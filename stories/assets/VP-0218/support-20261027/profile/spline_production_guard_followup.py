from pathlib import Path
import csv,hashlib,json
import numpy as np
from scipy.optimize import linprog
from scipy.interpolate import BSpline
from scipy import ndimage
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1');ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay');OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile')
H,W=293,3840;TX,TY=64,32;TAU=.25;DEG=3;NX,NY=12,6

def knots(n):return np.r_[[0.]*(DEG+1),np.linspace(0,1,n-DEG+1)[1:-1],[1.]*(DEG+1)]
KX,KY=knots(NX),knots(NY)
def b1(z,k):return BSpline.design_matrix(np.asarray(z,float),k,DEG,extrapolate=True).toarray()
def design_points(x,y):return np.einsum('ni,nj->nij',b1(np.asarray(x,float)/(W-1),KX),b1(np.asarray(y,float)/(H-1),KY)).reshape(len(x),NX*NY)
def fit(X,z):
 n,p=X.shape;c=np.r_[np.zeros(p),np.full(n,TAU),np.full(n,1-TAU)];A=np.c_[X,np.eye(n),-np.eye(n)];r=linprog(c,A_eq=A,b_eq=z,bounds=[(None,None)]*p+[(0,None)]*(2*n),method='highs')
 if not r.success:raise RuntimeError(r.message)
 return r.x[:p]
def predict(beta,x0,y0,x1,y1):return np.einsum('xi,yj,ij->yx',b1(np.arange(x0,x1)/(W-1),KX),b1(np.arange(y0,y1)/(H-1),KY),beta.reshape(NX,NY),optimize=True)
def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();P=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,P.shape[1],6),np.uint16);U=np.empty((h,P.shape[1],3),np.uint16);V=np.empty_like(U)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(P[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(float),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(float),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(float)
def pct(a,q):a=np.sort(np.asarray(a).ravel());return float(a[int(q*(len(a)-1))])
def stats(y,u,v,res,a,b,side):
 h,w=y.shape;depth=b-a;yy=((2*np.arange(6)+1)*depth)//12;yy=yy if side=='top' else h-1-yy;xx=((2*np.arange(48)+1)*w)//96;sy=y[np.ix_(yy,xx)];su=u[np.ix_(yy,xx)];sv=v[np.ix_(yy,xx)];sr=res[np.ix_(yy-a,xx)];texture=float(np.abs(np.diff(sy.astype(float),axis=1)).mean());neutral=float(((abs(su-512)<=32)&(abs(sv-512)<=32)).mean())
 def summary(vals,threshold):
  black=vals<=threshold;cont=float((black.sum(axis=1)>=44).mean());m={'black_fraction':float(black.mean()),'p90':pct(vals,.9),'dispersion':pct(vals,.9)-pct(vals,.1),'raw_texture':texture,'raw_neutral_fraction':neutral,'continuity':cont};m['summary_thresholds_pass']=bool(m['black_fraction']>=.95 and m['p90']<=threshold and m['dispersion']<=24 and texture<=8 and neutral>=.90 and cont>=.99);return m
 vy=((2*np.arange(min(64,depth))+1)*depth)//(2*min(64,depth));vy=vy if side=='top' else h-1-vy;vx=((2*np.arange(256)+1)*w)//512;ay=y[np.ix_(vy,vx)];au=u[np.ix_(vy,vx)];av=v[np.ix_(vy,vx)];ar=res[np.ix_(vy-a,vx)];color=(np.maximum(abs(au-512),abs(av-512))>=64)&(ay>=96)
 def visible(mask):
  counts=mask.sum(axis=1);occ=counts>=2;return {'visible_samples':int(mask.sum()),'depth_rows_ge2':int(occ.sum()),'adjacent_occupied_depth_rows':bool((occ[:-1]&occ[1:]).any()),'peak_count_per_depth':int(counts.max())}
 return {'original_summary':summary(sy,88),'spline_residual_summary':summary(sr,24),'raw_visible':visible((ay>96)|color),'spline_relative_visible':visible((ar>32)|color),'sample_count':int(sy.size),'visibility_sample_count':int(ay.size),'color_raw_witness_samples':int(color.sum())}
rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));unique={}
for row in rows:unique.setdefault(row['SHA256'],row['File'])
tiles=[(x0,y0,min(W,x0+TX),min(H,y0+TY),(x0//TX+y0//TY)%2) for y0 in range(0,H,TY) for x0 in range(0,W,TX)];records=[]
for sha,name in unique.items():
 y,u,v=decode(ROOT/name);held=np.zeros((H,W),float)
 for parity in (0,1):
  train=[t for t in tiles if t[4]==parity];valid=[t for t in tiles if t[4]!=parity];xs=[];ys=[];targets=[]
  for x0,y0,x1,y1,_ in train:xs.append((x0+x1-1)/2);ys.append((y0+y1-1)/2);targets.append(float(np.percentile(y[y0:y1,x0:x1],25)))
  beta=fit(np.stack([design_points([x],[yy])[0] for x,yy in zip(xs,ys)]),np.asarray(targets))
  for x0,y0,x1,y1,_ in valid:held[y0:y1,x0:x1]=y[y0:y1,x0:x1]-predict(beta,x0,y0,x1,y1)
 top_raw=y[:H,:]-ndimage.median_filter(y[:H,:],size=(17,17),mode='reflect');top=stats(y,u,v,held,0,H,'top');bottom_res=y[1865:,:]-ndimage.median_filter(y[1865:,:],size=(17,17),mode='reflect');bottom=stats(y,u,v,bottom_res,1865,2160,'bottom')
 records.append({'file':name,'sha256':sha,'top':top,'bottom_uniform_reference':bottom,'raw_top_residual17_abs_max':float(np.max(np.abs(top_raw))),'raw_top_residual17_abs_p99':float(np.percentile(np.abs(top_raw),99)),'retained_raw_top_source_not_modified':True})
report={'protocol_sha256':hashlib.sha256((OUT/'spline-surface-protocol.txt').read_bytes()).hexdigest(),'guard_formula_source_sha256':hashlib.sha256((OUT.parent/'reference'/'compare_reference.py').read_bytes()).hexdigest(),'method':'Exact same 6x48 summary and 64x256 visible-witness coordinates/formulas as compare_reference.py; only top local reference residual uses checkerboard-heldout spline. Raw source metrics remain unchanged; bottom shown with original local17 residual only as a control, not spline-fit.','records':records}
(OUT/'spline-production-guard-results.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
