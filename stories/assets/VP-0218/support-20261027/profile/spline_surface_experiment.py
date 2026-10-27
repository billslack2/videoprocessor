from pathlib import Path
import csv,hashlib,json,time
import numpy as np
from scipy.optimize import linprog
from scipy.interpolate import BSpline
from scipy import ndimage
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1');ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay');OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile')
H,W=293,3840;TX,TY=64,32;TAU=.25;DEG=3;NX,NY=12,6

def knots(n):
 internal=np.linspace(0,1,n-DEG+1)[1:-1]
 return np.r_[[0.]*(DEG+1),internal,[1.]*(DEG+1)]
KX,KY=knots(NX),knots(NY)
def basis_1d(z,k):return BSpline.design_matrix(np.asarray(z,dtype=float),k,DEG,extrapolate=True).toarray()
def design_points(x,y):
 bx=basis_1d(2*np.asarray(x,dtype=float)/(W-1)-1+1 if False else np.asarray(x,dtype=float)/(W-1),KX)
 by=basis_1d(np.asarray(y,dtype=float)/(H-1),KY)
 return np.einsum('ni,nj->nij',bx,by).reshape(len(bx),NX*NY)
def fit_q(X,z):
 n,p=X.shape;c=np.r_[np.zeros(p),np.full(n,TAU),np.full(n,1-TAU)];A=np.c_[X,np.eye(n),-np.eye(n)];r=linprog(c,A_eq=A,b_eq=z,bounds=[(None,None)]*p+[(0,None)]*(2*n),method='highs')
 if not r.success:raise RuntimeError(r.message)
 return r.x[:p],float(r.fun)
def pct(a):return {str(q):float(np.percentile(a,q)) for q in (10,50,90,95,99,99.9,100)}
def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();P=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,P.shape[1],6),np.uint16);U=np.empty((h,P.shape[1],3),np.uint16);V=np.empty_like(U)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(P[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(float),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(float),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(float)
def predict_tile(beta,x0,y0,x1,y1):
 bx=basis_1d(np.arange(x0,x1)/(W-1),KX);by=basis_1d(np.arange(y0,y1)/(H-1),KY);coef=beta.reshape(NX,NY)
 return np.einsum('xi,yj,ij->yx',bx,by,coef,optimize=True)
def comp_summary(mask):
 lab,n=ndimage.label(mask,structure=np.array([[0,1,0],[1,1,1],[0,1,0]],np.uint8));areas=[]
 for i,sl in enumerate(ndimage.find_objects(lab),1):
  if sl is None:continue
  a=int((lab[sl]==i).sum())
  if a>=4:areas.append(a)
 return areas

tiles=[(x0,y0,min(W,x0+TX),min(H,y0+TY),(x0//TX+y0//TY)%2) for y0 in range(0,H,TY) for x0 in range(0,W,TX)]
rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));uniq={}
for row in rows:uniq.setdefault(row['SHA256'],row['File'])
report={'protocol_sha256':hashlib.sha256((OUT/'spline-surface-protocol.txt').read_bytes()).hexdigest(),'basis':{'degree':DEG,'nx':NX,'ny':NY,'knot_x':KX.tolist(),'knot_y':KY.tolist(),'coefficient_count':NX*NY,'coordinate_domain':[0,1]},'tile_grid':{'width':TX,'height':TY,'train_tile_quantile':TAU,'crossvalidation':'checkerboard tile parity; both directions'},'records':[]}
t0=time.time()
for digest,name in uniq.items():
 y,u,v=decode(ROOT/name);Y=y[:H,:];held=np.zeros((H,W),np.float32);predfull=np.zeros((H,W),np.float32);folds=[];texture=[];rawtextures=[];components=[];surface_ranges=[]
 raw_hp=np.abs(Y-ndimage.median_filter(Y,size=(17,17),mode='reflect'));rawtextures.extend(raw_hp[8:-8,8:-8].ravel())
 for parity in (0,1):
  train=[t for t in tiles if t[4]==parity];valid=[t for t in tiles if t[4]!=parity];XX=[];zz=[]
  for x0,y0,x1,y1,p in train:
   XX.append(design_points([(x0+x1-1)/2],[(y0+y1-1)/2])[0]);zz.append(float(np.percentile(Y[y0:y1,x0:x1],25)))
  beta,obj=fit_q(np.stack(XX),np.array(zz));nval=0;foldres=[];foldtex=[];foldcomps=[];foldmin=1e9;foldmax=-1e9
  for x0,y0,x1,y1,p in valid:
   pr=predict_tile(beta,x0,y0,x1,y1);rr=(Y[y0:y1,x0:x1]-pr).astype(np.float32);held[y0:y1,x0:x1]=rr;predfull[y0:y1,x0:x1]=pr;nval+=rr.size;foldres.append(rr.ravel());foldmin=min(foldmin,float(pr.min()));foldmax=max(foldmax,float(pr.max()))
   if rr.shape[0]>16 and rr.shape[1]>16:
    hp=np.abs(rr-ndimage.median_filter(rr,size=(17,17),mode='reflect'));inner=hp[8:-8,8:-8];foldtex.append(inner.ravel());texture.extend(inner.ravel())
   areas=comp_summary(np.abs(rr)>=12);foldcomps.extend(areas);components.extend(areas)
  resv=np.concatenate(foldres);folds.append({'validation_parity':1-parity,'train_tiles':len(train),'validation_tiles':len(valid),'coefficients':beta.tolist(),'train_tile_q25_objective':obj,'heldout_pixels':nval,'surface_validation_range':[foldmin,foldmax],'heldout_residual_signed_percentiles':pct(resv),'heldout_residual_abs_percentiles':pct(np.abs(resv)),'validation_component_count_area_ge4_at_abs12':len(foldcomps),'validation_component_area_max':max(foldcomps,default=0)})
  surface_ranges.append([foldmin,foldmax])
 R=held;absR=np.abs(R);ch=np.maximum(np.abs(u[:H,:]-512),np.abs(v[:H,:]-512));T=np.asarray(texture);rt=np.asarray(rawtextures)
 rec={'file':name,'sha256':digest,'heldout_pixel_count':H*W,'surface_range_union':[min(a[0] for a in surface_ranges),max(a[1] for a in surface_ranges)],'heldout_residual_signed_percentiles':pct(R),'heldout_residual_abs_percentiles':pct(absR),'heldout_residual_p90_minus_p10':float(np.percentile(R,90)-np.percentile(R,10)),'heldout_fraction_abs_within4':float((absR<=4).mean()),'unchanged_profile_residual_tolerances':{'abs_p90_le24':bool(np.percentile(absR,90)<=24),'signed_p90_minus_p10_le24':bool(np.percentile(R,90)-np.percentile(R,10)<=24),'abs_p95_le4_constant_ref':bool(np.percentile(absR,95)<=4),'abs_p99_le12':bool(np.percentile(absR,99)<=12),'every_pixel_pm4':bool(np.all(absR<=4))},'heldout_local_texture_abs17_percentiles_tile_interiors':pct(T),'heldout_texture_p95_le8':bool(np.percentile(T,95)<=8),'heldout_texture_p99_le8':bool(np.percentile(T,99)<=8),'raw_texture_abs17_percentiles_tile_interiors':pct(rt),'raw_texture_p95_le8':bool(np.percentile(rt,95)<=8),'raw_texture_p99_le8':bool(np.percentile(rt,99)<=8),'raw_chroma_abs_deviation_p99_UV':[float(np.percentile(np.abs(u[:H,:]-512),99)),float(np.percentile(np.abs(v[:H,:]-512),99))],'raw_neutral_chroma_fraction_absuv16':float((ch<=16).mean()),'raw_y_percentiles':pct(Y),'raw_fraction_y_gt88':float((Y>88).mean()),'raw_count_y_gt88':int((Y>88).sum()),'heldout_abs_resid12_components_area_ge4_count_across_tiles':len(components),'heldout_abs_resid12_components_area_ge4_max':max(components,default=0),'folds':folds}
 report['records'].append(rec)
report['runtime_seconds']=round(time.time()-t0,2)
(OUT/'spline-surface-results.json').write_text(json.dumps(report,indent=2))
print(json.dumps({'protocol_sha256':report['protocol_sha256'],'runtime_seconds':report['runtime_seconds'],'records':[{'file':r['file'],'surface':r['surface_range_union'],'signed':r['heldout_residual_signed_percentiles'],'abs':r['heldout_residual_abs_percentiles'],'p90_minus10':r['heldout_residual_p90_minus_p10'],'within4':r['heldout_fraction_abs_within4'],'resid_tolerances':r['unchanged_profile_residual_tolerances'],'texture_p95_p99':r['heldout_local_texture_abs17_percentiles_tile_interiors'],'texture8passes':[r['heldout_texture_p95_le8'],r['heldout_texture_p99_le8']],'raw_texture_p95p99':r['raw_texture_abs17_percentiles_tile_interiors'],'raw_chroma_p99':r['raw_chroma_abs_deviation_p99_UV'],'neutral':r['raw_neutral_chroma_fraction_absuv16'],'raw_gt88':r['raw_fraction_y_gt88'],'held_components':r['heldout_abs_resid12_components_area_ge4_count_across_tiles']} for r in report['records']]},indent=2))
