from pathlib import Path
import csv,hashlib,json
import numpy as np
from scipy import ndimage
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1'); ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay'); OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile'); TOP=293

def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();W=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,W.shape[1],6),np.uint16);U=np.empty((h,W.shape[1],3),np.uint16);V=np.empty_like(U)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(W[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(W[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(W[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(np.int16),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16)
def pct(v):return {str(p):float(np.percentile(v,p)) for p in (10,50,90,95,99,99.9,100)}
def csummary(r):
 lab,n=ndimage.label(r>=12,structure=np.array([[0,1,0],[1,1,1],[0,1,0]],np.uint8));cs=[]
 for i,sl in enumerate(ndimage.find_objects(lab),1):
  if sl is None:continue
  mask=lab[sl]==i;area=int(mask.sum())
  if area<16:continue
  y,x=sl; vals=r[sl][mask];cs.append({'area':area,'bbox':[x.start,y.start,x.stop,y.stop],'peak':int(vals.max()),'p95':float(np.percentile(vals,95))})
 return cs
rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));uniq={}
for r in rows:uniq.setdefault(r['SHA256'],r['File'])
records=[]
for digest,name in uniq.items():
 y,u,v=decode(ROOT/name);band=y[:TOP,:].astype(np.float32)
 scalar=float(np.median(band)); scalar_res=band-scalar
 col=np.median(band,axis=0);ref=ndimage.median_filter(col,size=129,mode='reflect').astype(np.float32);local_res=band-ref[None,:]
 chroma=np.maximum(np.abs(u[:TOP,:]-512),np.abs(v[:TOP,:]-512))
 tiles=[]
 for yy in range(0,TOP,32):
  for xx in range(0,3840,64):
   z=local_res[yy:min(yy+32,TOP),xx:xx+64]
   tiles.append({'x':xx,'y':yy,'p90_minus_p10':float(np.percentile(z,90)-np.percentile(z,10)),'p95_abs':float(np.percentile(np.abs(z),95)),'max_abs':float(np.max(np.abs(z)))})
 cs=csummary(np.abs(local_res))
 records.append({'file':name,'sha256':digest,'raw_top_y_percentiles':pct(band),'global_scalar_reference':scalar,'global_scalar_residual_percentiles':pct(scalar_res),'local_column_profile_range':[float(ref.min()),float(ref.max())],'local_signed_residual_percentiles':pct(local_res),'local_abs_residual_percentiles':pct(np.abs(local_res)),'local_abs_p90_minus_p10':float(np.percentile(np.abs(local_res),90)-np.percentile(np.abs(local_res),10)),'local_fraction_abs_resid_le4':float((np.abs(local_res)<=4).mean()),'local_every_pixel_within_pm4':bool(np.all(np.abs(local_res)<=4)),'existing_p90p10_le4_pass':bool(np.percentile(local_res,90)-np.percentile(local_res,10)<=4),'existing_profile_exact_sample_tolerance_pass':bool(np.all(np.abs(local_res)<=4)),'neutral_chroma_fraction_absuv16':float((chroma<=16).mean()),'raw_pixels_above_88_count':int((band>88).sum()),'raw_pixels_above_88_fraction':float((band>88).mean()),'local_residual_components_ge16_at12':len(cs),'local_residual_components':cs,'fixed_64x32_tiles':tiles})
report={'protocol_sha256':hashlib.sha256((OUT/'local-reference-protocol.txt').read_bytes()).hexdigest(),'model':'column median across 293 top rows, then reflected 129-column median smoothing; no source pixels altered','raw_floor_plus24_code':88,'records':records,'interpretation_limits':['Native-only calibration from one clip; four unique payloads are not independent scenes.','The per-column model is fit to the candidate exterior itself and can absorb content that spans many rows; all raw above-floor and residual evidence remains separately reported.','Model residual fit and acquisition safety are separate. No bounded per-edge acquisition authority is inferred.']}
(OUT/'local-reference-results.json').write_text(json.dumps(report,indent=2))
print(json.dumps({'protocol_sha256':report['protocol_sha256'],'records':[{'file':r['file'],'raw_y':r['raw_top_y_percentiles'],'reference_range':r['local_column_profile_range'],'resid_signed':r['local_signed_residual_percentiles'],'resid_abs':r['local_abs_residual_percentiles'],'fraction_abs_le4':r['local_fraction_abs_resid_le4'],'p90p10_pass':r['existing_p90p10_le4_pass'],'all_pixel_pm4':r['existing_profile_exact_sample_tolerance_pass'],'neutral':r['neutral_chroma_fraction_absuv16'],'above88':r['raw_pixels_above_88_fraction'],'residual_components':r['local_residual_components_ge16_at12']} for r in records]},indent=2))
