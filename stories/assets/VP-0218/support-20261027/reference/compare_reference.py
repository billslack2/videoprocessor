from pathlib import Path
import hashlib,json,csv
import numpy as np
from scipy import ndimage
old=Path(r'E:\codex\diagnostics\vp-0218-boundary-research-20261027\profile\component_shape_analysis.py')
ns={};exec(compile(old.read_text().split('rows=list(csv.DictReader',1)[0],str(old),'exec'),ns)
out=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\reference')
root=ns['ROOT'];rows=list(csv.DictReader((ns['ARCH']/'capture-frame-hashes.csv').open(encoding='utf-8-sig'))); unique={}
for row in rows:unique.setdefault(row['SHA256'],row['File'])
def pct(a,q):
 a=np.sort(np.asarray(a).ravel());return float(a[int(q*(len(a)-1))])
def stats(y,u,v,res,a,b,side):
 h,w=y.shape; depth=b-a; yy=((2*np.arange(6)+1)*depth)//12
 yy=yy if side=='top' else h-1-yy
 xx=((2*np.arange(48)+1)*w)//96
 sy=y[np.ix_(yy,xx)];su=u[np.ix_(yy,xx)];sv=v[np.ix_(yy,xx)];sr=res[np.ix_(yy-a,xx)]
 texture=float(np.abs(np.diff(sy.astype(float),axis=1)).mean());neutral=float(((abs(su-512)<=32)&(abs(sv-512)<=32)).mean())
 boundary=b if side=='top' else a
 insideY=boundary+2 if side=='top' else boundary-3
 contrast=float(y[insideY,xx].mean()-sy.mean())
 def summarize(vals,threshold,floor):
  black=vals<=threshold;continuity=float((black.sum(axis=1)>=44).mean())
  metrics={'black_fraction':float(black.mean()),'p90':pct(vals,.9),'dispersion':pct(vals,.9)-pct(vals,.1),'raw_texture':texture,'raw_neutral_fraction':neutral,'continuity':continuity,'raw_boundary_contrast_at_guard':contrast}
  metrics['summary_thresholds_pass']=bool(metrics['black_fraction']>=.95 and metrics['p90']<=threshold and metrics['dispersion']<=24 and texture<=8 and neutral>=.90 and continuity>=.99)
  return metrics
 vy=((2*np.arange(min(64,depth))+1)*depth)//(2*min(64,depth));vy=vy if side=='top' else h-1-vy
 vx=((2*np.arange(256)+1)*w)//512
 ay=y[np.ix_(vy,vx)];au=u[np.ix_(vy,vx)];av=v[np.ix_(vy,vx)];ar=res[np.ix_(vy-a,vx)]
 color=(np.maximum(abs(au-512),abs(av-512))>=64)&(ay>=96)
 def visible(mask):
  counts=mask.sum(axis=1);occupied=counts>=2
  return {'visible_samples':int(mask.sum()),'depth_rows_ge2':int(occupied.sum()),'adjacent_occupied_depth_rows':bool((occupied[:-1]&occupied[1:]).any()),'peak_count_per_depth':int(counts.max())}
 return {'side':side,'band':[a,b],'original':summarize(sy,88,64),'reference_residual':summarize(sr,24,0),'raw_visible':visible((ay>96)|color),'reference_relative_visible':visible((ar>32)|color),'sample_count':int(sy.size),'visibility_sample_count':int(ay.size),'notes':'Contrast is at retained guard coordinate, not nominal edge; unchanged acquisition contrast requirement may fail inside the fringe. Residual model is self-fitted and not a certificate.'}
records=[]
for sha,name in unique.items():
 y,u,v=ns['decode'](root/name);record={'file':name,'sha256':sha,'edges':[]}
 for a,b,side in [(0,293,'top'),(1865,2160,'bottom')]:
  res=y[a:b]-ndimage.median_filter(y[a:b],size=17,mode='reflect')
  record['edges'].append(stats(y,u,v,res,a,b,side))
 records.append(record)
payload={'protocol_sha256':hashlib.sha256((out/'protocol.txt').read_bytes()).hexdigest(),'method':'reference-residual diagnostic on original sample coordinates; not a production classifier','records':records}
(out/'results.json').write_text(json.dumps(payload,indent=2))
print(json.dumps(payload,indent=2))
