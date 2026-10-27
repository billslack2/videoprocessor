from pathlib import Path
import csv,json
import numpy as np
from scipy.optimize import linprog
from scipy.interpolate import BSpline
from scipy import ndimage
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1');ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay');OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile')
H,W=293,3840;TX,TY=64,32;TAU=.25;DEG=3;NX,NY=12,6
def knots(n):return np.r_[[0.]*(DEG+1),np.linspace(0,1,n-DEG+1)[1:-1],[1.]*(DEG+1)]
KX,KY=knots(NX),knots(NY)
def b1(z,k):return BSpline.design_matrix(np.asarray(z,float),k,DEG,extrapolate=True).toarray()
def design(x,y):return np.einsum('ni,nj->nij',b1(np.asarray(x,float)/(W-1),KX),b1(np.asarray(y,float)/(H-1),KY)).reshape(len(x),NX*NY)
def fit(X,z):
 n,p=X.shape;c=np.r_[np.zeros(p),np.full(n,TAU),np.full(n,1-TAU)];A=np.c_[X,np.eye(n),-np.eye(n)];r=linprog(c,A_eq=A,b_eq=z,bounds=[(None,None)]*p+[(0,None)]*(2*n),method='highs')
 if not r.success:raise RuntimeError(r.message)
 return r.x[:p]
def pred(beta,x0,y0,x1,y1):return np.einsum('xi,yj,ij->yx',b1(np.arange(x0,x1)/(W-1),KX),b1(np.arange(y0,y1)/(H-1),KY),beta.reshape(NX,NY),optimize=True)
def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();P=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,P.shape[1],6),np.uint16);U=np.empty((h,P.shape[1],3),np.uint16);V=np.empty_like(U)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(P[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(float),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(float),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(float)
def full_heldout(y):
 tiles=[(x0,y0,min(W,x0+TX),min(H,y0+TY),(x0//TX+y0//TY)%2) for y0 in range(0,H,TY) for x0 in range(0,W,TX)];R=np.zeros((H,W),float);P=np.zeros_like(R)
 for parity in (0,1):
  train=[t for t in tiles if t[4]==parity];valid=[t for t in tiles if t[4]!=parity];xs=[];ys=[];z=[]
  for x0,y0,x1,y1,_ in train:xs.append((x0+x1-1)/2);ys.append((y0+y1-1)/2);z.append(float(np.percentile(y[y0:y1,x0:x1],25)))
  beta=fit(np.stack([design([x],[yy])[0] for x,yy in zip(xs,ys)]),np.array(z))
  for x0,y0,x1,y1,_ in valid:
   pr=pred(beta,x0,y0,x1,y1);P[y0:y1,x0:x1]=pr;R[y0:y1,x0:x1]=y[y0:y1,x0:x1]-pr
 return P,R
rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));uniq={}
for row in rows:uniq.setdefault(row['SHA256'],row['File'])
records=[]
for sha,name in uniq.items():
 y,u,v=decode(ROOT/name);band=y[:H,:];P,R=full_heldout(band);med=ndimage.median_filter(band,size=(17,17),mode='reflect');local=band-med;lab,n=ndimage.label(np.abs(local)>=12,structure=np.array([[0,1,0],[1,1,1],[0,1,0]],np.uint8));depth=H;vy=((2*np.arange(min(64,depth))+1)*depth)//(2*min(64,depth));vx=((2*np.arange(256)+1)*W)//512;rawS=band[np.ix_(vy,vx)];pS=P[np.ix_(vy,vx)];rS=R[np.ix_(vy,vx)];locS=local[np.ix_(vy,vx)];uS=u[np.ix_(vy,vx)];vS=v[np.ix_(vy,vx)];color=(np.maximum(abs(uS-512),abs(vS-512))>=64)&(rawS>=96);visible=(rS>32)|color;counts=visible.sum(axis=1);points=[]
 for yi,xi in zip(*np.nonzero(visible)):
  y0,x0=int(vy[yi]),int(vx[xi]);li=int(lab[y0,x0]); bbox=None
  if li:
   ys,xs=ndimage.find_objects(lab)[li-1];bbox=[int(xs.start),int(ys.start),int(xs.stop),int(ys.stop)]
  points.append({'probe_row_index':int(yi),'probe_col_index':int(xi),'x':x0,'y':y0,'raw_Y':float(rawS[yi,xi]),'spline_prediction_Y':float(pS[yi,xi]),'heldout_residual_R':float(rS[yi,xi]),'local17_prediction_Y':float(med[y0,x0]),'local17_signed_residual':float(locS[yi,xi]),'local17_abs_residual_ge12_component_id':li,'local17_component_bbox_xyxy':bbox,'raw_color_witness':bool(color[yi,xi])})
 adjacent=[{'row_index':i,'y':int(vy[i]),'visible_count':int(counts[i]),'points':[p for p in points if p['probe_row_index']==i]} for i in range(len(vy)-1) if counts[i]>=2 and counts[i+1]>=2]
 records.append({'file':name,'sha256':sha,'heldout_visible_sample_count':len(points),'visible_probe_row_counts':[{'row':int(vy[i]),'count':int(c)} for i,c in enumerate(counts) if c>0],'adjacent_occupied_rows':adjacent,'all_27_or_fewer_probe_points':points})
report={'fixed_model_protocol_sha256':__import__('hashlib').sha256((OUT/'spline-surface-protocol.txt').read_bytes()).hexdigest(),'purpose':'Diagnostic coordinate attribution only; no threshold/gate changes. Spline-relative visible witness is residual>32 or original raw color witness; compare exact probe positions with original abs(Y-median17)>=12 components.','records':records}
(OUT/'spline-visible-probe-coordinates.json').write_text(json.dumps(report,indent=2));print(json.dumps({'per_frame':[{'file':r['file'],'count':r['heldout_visible_sample_count'],'rows':r['visible_probe_row_counts'],'adjacent':r['adjacent_occupied_rows'],'points':r['all_27_or_fewer_probe_points']} for r in records]},indent=2))
