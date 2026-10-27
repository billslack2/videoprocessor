# Frozen-model edge-summary residual follow-up; model, tiles, folds and tau exactly match surface_fit_experiment.py.
from pathlib import Path
import csv,hashlib,json
import numpy as np
from scipy.optimize import linprog
from scipy import ndimage
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1');ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay');OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile');H,W=293,3840;TX,TY=64,32;TAU=.25;TERMS=[(i,j) for i in range(4) for j in range(4-i)]
def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();P=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,P.shape[1],6),np.uint16);U=np.empty((h,P.shape[1],3),np.uint16);V=np.empty_like(U)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(P[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(float),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(float),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(float)
def design(x,y):
 xx=2*np.asarray(x,dtype=float)/(W-1)-1;yy=2*np.asarray(y,dtype=float)/(H-1)-1
 return np.stack([np.polynomial.chebyshev.chebval(xx,[0]*i+[1])*np.polynomial.chebyshev.chebval(yy,[0]*j+[1]) for i,j in TERMS],axis=-1)
def fit(X,z):
 n,p=X.shape;c=np.r_[np.zeros(p),np.full(n,TAU),np.full(n,1-TAU)];A=np.c_[X,np.eye(n),-np.eye(n)];r=linprog(c,A_eq=A,b_eq=z,bounds=[(None,None)]*p+[(0,None)]*(2*n),method='highs')
 if not r.success:raise RuntimeError(r.message)
 return r.x[:p]
def pct(a):return {str(q):float(np.percentile(a,q)) for q in (10,50,90,95,99,99.9,100)}
rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));uniq={}
for row in rows:uniq.setdefault(row['SHA256'],row['File'])
records=[]
for digest,name in uniq.items():
 y,u,v=decode(ROOT/name);Y=y[:H,:];resids=[];textures=[];tile_summary=[];surfaces=[]
 tiles=[(x0,y0,min(W,x0+TX),min(H,y0+TY),(x0//TX+y0//TY)%2) for y0 in range(0,H,TY) for x0 in range(0,W,TX)]
 for parity in (0,1):
  train=[t for t in tiles if t[4]==parity];valid=[t for t in tiles if t[4]!=parity];xc=[];yc=[];z=[]
  for x0,y0,x1,y1,p in train:xc.append((x0+x1-1)/2);yc.append((y0+y1-1)/2);z.append(float(np.percentile(Y[y0:y1,x0:x1],25)))
  beta=fit(design(np.array(xc),np.array(yc)),np.array(z));surfaces.append(beta.tolist())
  for x0,y0,x1,y1,p in valid:
   XX,YY=np.meshgrid(np.arange(x0,x1),np.arange(y0,y1));pred=(design(XX,YY)@beta).reshape(YY.shape);rr=Y[y0:y1,x0:x1]-pred;resids.append(rr.ravel())
   # Compute texture only on tile interior so neither the held-out tile boundary nor fold seams enter.
   if rr.shape[0]>16 and rr.shape[1]>16:
    hp=np.abs(rr-ndimage.median_filter(rr,size=(17,17),mode='reflect'))
    textures.append(hp[8:-8,8:-8].ravel())
   tile_summary.append({'parity':parity,'x':x0,'y':y0,'resid_p90_minus_p10':float(np.percentile(rr,90)-np.percentile(rr,10)),'resid_abs_p90':float(np.percentile(np.abs(rr),90)),'texture_interior_p95':float(np.percentile(hp[8:-8,8:-8],95)) if rr.shape[0]>16 and rr.shape[1]>16 else None})
 R=np.concatenate(resids);T=np.concatenate(textures);chroma=np.maximum(np.abs(u[:H,:]-512),np.abs(v[:H,:]-512));
 records.append({'file':name,'heldout_residual_signed_percentiles':pct(R),'heldout_residual_abs_percentiles':pct(np.abs(R)),'heldout_residual_p90_minus_p10':float(np.percentile(R,90)-np.percentile(R,10)),'unchanged_residual_limits':{'abs_p90_le24_pass':bool(np.percentile(np.abs(R),90)<=24),'signed_p90_minus_p10_le24_pass':bool(np.percentile(R,90)-np.percentile(R,10)<=24),'abs_p95_le4_constant_profile_pass':bool(np.percentile(np.abs(R),95)<=4),'abs_p99_le12_pass':bool(np.percentile(np.abs(R),99)<=12),'every_pixel_pm4_pass':bool(np.all(np.abs(R)<=4))},'heldout_local_texture17_abs_percentiles_tile_interiors':pct(T),'texture_p95_le8_pass':bool(np.percentile(T,95)<=8),'texture_p99_le8_pass':bool(np.percentile(T,99)<=8),'raw_chroma_deviation_abs_p99_UV':[float(np.percentile(np.abs(u[:H,:]-512),99)),float(np.percentile(np.abs(v[:H,:]-512),99))],'raw_neutral_chroma_fraction_absuv16':float((chroma<=16).mean()),'tile_residuals':tile_summary})
report={'surface_model_sha256':hashlib.sha256((OUT/'surface_fit_experiment.py').read_bytes()).hexdigest(),'protocol_sha256':hashlib.sha256((OUT/'surface-fit-protocol.txt').read_bytes()).hexdigest(),'method_note':'Same frozen 10-coefficient cubic tensor Chebyshev quantile model, checkerboard folds. Texture is abs(residual17 - reflected median17(residual17)) measured inside validation tile margins of 8 pixels; reported separately. Raw chroma is unchanged.','records':records}
(OUT/'surface-fit-edge-summary.json').write_text(json.dumps(report,indent=2));print(json.dumps({'model_sha256':report['surface_model_sha256'],'records':[{'file':r['file'],'residual_signed':r['heldout_residual_signed_percentiles'],'abs':r['heldout_residual_abs_percentiles'],'residual_tolerances':r['unchanged_residual_limits'],'texture':r['heldout_local_texture17_abs_percentiles_tile_interiors'],'texture_passes':[r['texture_p95_le8_pass'],r['texture_p99_le8_pass']],'chroma_p99':r['raw_chroma_deviation_abs_p99_UV'],'neutral':r['raw_neutral_chroma_fraction_absuv16']} for r in records]},indent=2))
