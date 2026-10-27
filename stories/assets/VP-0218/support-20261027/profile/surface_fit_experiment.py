from pathlib import Path
import csv,hashlib,json,time
import numpy as np
from scipy.optimize import linprog
from scipy import ndimage
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1'); ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay'); OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile')
H,W=293,3840; THETA=.25; TY,TX=32,64
TERMS=[(i,j) for i in range(4) for j in range(4-i)]
def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();P=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,P.shape[1],6),np.uint16);U=np.empty((h,P.shape[1],3),np.uint16);V=np.empty_like(U)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(P[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(float),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(float),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(float)
def design(x,y):
 xx=2*np.asarray(x,dtype=float)/ (W-1)-1; yy=2*np.asarray(y,dtype=float)/(H-1)-1
 return np.stack([np.polynomial.chebyshev.chebval(xx,[0]*i+[1])*np.polynomial.chebyshev.chebval(yy,[0]*j+[1]) for i,j in TERMS],axis=-1)
def fit_quantile(X,z):
 n,p=X.shape; c=np.r_[np.zeros(p), np.full(n,THETA),np.full(n,1-THETA)]
 Aeq=np.c_[X,np.eye(n),-np.eye(n)]
 res=linprog(c,A_eq=Aeq,b_eq=z,bounds=[(None,None)]*p+[(0,None)]*(2*n),method='highs')
 if not res.success:raise RuntimeError(res.message)
 return res.x[:p],float(res.fun)
def tiles():
 out=[]
 for y0 in range(0,H,TY):
  for x0 in range(0,W,TX):out.append((x0,y0,min(W,x0+TX),min(H,y0+TY),(x0//TX+y0//TY)%2))
 return out
def pct(a):return {str(q):float(np.percentile(a,q)) for q in (10,50,90,95,99,99.9,100)}
def component_stats(mask):
 lab,n=ndimage.label(mask,structure=np.array([[0,1,0],[1,1,1],[0,1,0]],np.uint8));cs=[]
 for i,sl in enumerate(ndimage.find_objects(lab),1):
  if sl is None:continue
  a=lab[sl]==i;area=int(a.sum())
  if area<4:continue
  yy,xx=sl;cs.append({'area':area,'bbox':[xx.start,yy.start,xx.stop,yy.stop],'width':xx.stop-xx.start,'height':yy.stop-yy.start})
 return cs
alltiles=tiles(); rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));uniq={}
for row in rows:uniq.setdefault(row['SHA256'],row['File'])
report={'protocol_sha256':hashlib.sha256((OUT/'surface-fit-protocol.txt').read_bytes()).hexdigest(),'model_terms':TERMS,'tile_grid':{'width':TX,'height':TY,'nx':int(np.ceil(W/TX)),'ny':int(np.ceil(H/TY)),'training_tile_target_quantile':THETA,'cross_validation':'tile parity (ix+iy)%2; fit parity, validate opposite; reverse'},'records':[]}
t0=time.time()
for digest,name in uniq.items():
 y,u,v=decode(ROOT/name);Y=y[:H,:];held_res=np.zeros((H,W),np.float32);held_pred=np.zeros((H,W),np.float32);folds=[]
 for parity in (0,1):
  train=[];valid=[]
  for x0,y0,x1,y1,p in alltiles:
   (train if p==parity else valid).append((x0,y0,x1,y1,p))
  xx=[];yy=[];zz=[]
  for x0,y0,x1,y1,p in train:
   xx.append((x0+x1-1)/2);yy.append((y0+y1-1)/2);zz.append(float(np.percentile(Y[y0:y1,x0:x1],25)))
  X=design(np.array(xx),np.array(yy));beta,obj=fit_quantile(X,np.array(zz))
  xgrid=np.arange(W); ygrid=np.arange(H);Xfull=design(np.broadcast_to(xgrid[None,:],(H,W)),np.broadcast_to(ygrid[:,None],(H,W)));pred=(Xfull@beta).reshape(H,W)
  vmask=np.zeros((H,W),bool);tile_res=[];nval=0;maxper=[];ncomps=0
  for x0,y0,x1,y1,p in valid:
   vmask[y0:y1,x0:x1]=True;rr=Y[y0:y1,x0:x1]-pred[y0:y1,x0:x1];tile_res.append({'x':x0,'y':y0,'signed_p10_p50_p90':[float(np.percentile(rr,10)),float(np.percentile(rr,50)),float(np.percentile(rr,90))],'abs_p95':float(np.percentile(np.abs(rr),95)),'abs_p99':float(np.percentile(np.abs(rr),99)),'fraction_abs_le4':float((np.abs(rr)<=4).mean()),'fraction_abs_ge12':float((np.abs(rr)>=12).mean())});nval+=rr.size
   cs=component_stats(np.abs(rr)>=12);ncomps+=len(cs);maxper.extend(cs)
  held_res[vmask]=Y[vmask]-pred[vmask];held_pred[vmask]=pred[vmask]
  folds.append({'validation_parity':1-parity,'train_tiles':len(train),'validation_tiles':len(valid),'coefficients':beta.tolist(),'train_tile_q25_quantile_loss':obj,'validation_pixel_count':nval,'surface_range_all_rows_cols':[float(pred.min()),float(pred.max())],'validation_tiles_summary':tile_res,'validation_component_count_ge4':ncomps,'validation_components_ge4':maxper})
 chroma=np.maximum(np.abs(u[:H,:]-512),np.abs(v[:H,:]-512))
 # Candidate surface validation metrics: every pixel belongs to exactly one held-out checkerboard fold.
 model_abs=np.abs(held_res)
 rec={'file':name,'sha256':digest,'heldout_pixel_count':int(H*W),'heldout_signed_residual_percentiles':pct(held_res),'heldout_abs_residual_percentiles':pct(model_abs),'heldout_p90_minus_p10':float(np.percentile(held_res,90)-np.percentile(held_res,10)),'heldout_fraction_abs_within4':float((model_abs<=4).mean()),'existing_profile_tolerances':{'p95_abs_le4_pass':bool(np.percentile(model_abs,95)<=4),'p99_abs_le12_pass':bool(np.percentile(model_abs,99)<=12),'every_pixel_pm4_pass':bool(np.all(model_abs<=4))},'raw_y_percentiles':pct(Y),'raw_fraction_y_gt88':float((Y>88).mean()),'raw_count_y_gt88':int((Y>88).sum()),'raw_neutral_chroma_fraction_absuv16':float((chroma<=16).mean()),'heldout_residual_components_abs12_by_fold':sum(f['validation_component_count_ge4'] for f in folds),'folds':folds}
 report['records'].append(rec)
report['runtime_seconds']=round(time.time()-t0,2)
(OUT/'surface-fit-results.json').write_text(json.dumps(report,indent=2))
print(json.dumps({'protocol_sha256':report['protocol_sha256'],'runtime_seconds':report['runtime_seconds'],'records':[{'file':r['file'],'signed':r['heldout_signed_residual_percentiles'],'absolute':r['heldout_abs_residual_percentiles'],'p90minus10':r['heldout_p90_minus_p10'],'within4':r['heldout_fraction_abs_within4'],'tolerances':r['existing_profile_tolerances'],'raw_gt88':r['raw_fraction_y_gt88'],'neutral':r['raw_neutral_chroma_fraction_absuv16'],'components':r['heldout_residual_components_abs12_by_fold'],'fold_rough_ranges':[f['surface_range_all_rows_cols'] for f in r['folds']]} for r in report['records']]},indent=2))
