from pathlib import Path
import csv,json,hashlib
import numpy as np
from scipy import ndimage
root=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression');W,H=3840,2160
CASES={'city-lights':('city-lights.csv',66,2092),'thai-top-card':('thai-top-card.csv',274,1884),'latin-three-lines':('latin-three-lines.csv',274,1884)}
def pct(a,q):
 a=np.sort(np.asarray(a).ravel());return float(a[int(q*(len(a)-1))])
def stats(y,u,v,res,a,b,side,raw_threshold,visible_threshold):
 h,w=y.shape;depth=b-a;yy=((2*np.arange(6)+1)*depth)//12;yy=yy if side=='top' else h-1-yy;xx=((2*np.arange(48)+1)*w)//96
 sy=y[np.ix_(yy,xx)];su=u[np.ix_(yy,xx)];sv=v[np.ix_(yy,xx)];sr=res[np.ix_(yy-a,xx)]
 texture=float(np.abs(np.diff(sy.astype(float),axis=1)).mean());neutral=float(((abs(su-512)<=32)&(abs(sv-512)<=32)).mean());boundary=b if side=='top' else a;insideY=boundary+2 if side=='top' else boundary-3;contrast=float(y[insideY,xx].mean()-sy.mean())
 def summarize(vals,threshold):
  black=vals<=threshold;continuity=float((black.sum(axis=1)>=44).mean());metrics={'black_fraction':float(black.mean()),'p90':pct(vals,.9),'dispersion_p90_p10':pct(vals,.9)-pct(vals,.1),'raw_texture':texture,'raw_neutral_fraction':neutral,'continuity':continuity,'raw_boundary_contrast_at_guard':contrast};metrics['summary_thresholds_pass']=bool(metrics['black_fraction']>=.95 and metrics['p90']<=threshold and metrics['dispersion_p90_p10']<=24 and texture<=8 and neutral>=.90 and continuity>=.99);return metrics
 vy=((2*np.arange(min(64,depth))+1)*depth)//(2*min(64,depth));vy=vy if side=='top' else h-1-vy;vx=((2*np.arange(256)+1)*w)//512;ay=y[np.ix_(vy,vx)];au=u[np.ix_(vy,vx)];av=v[np.ix_(vy,vx)];ar=res[np.ix_(vy-a,vx)];color=(np.maximum(abs(au-512),abs(av-512))>=64)&(ay>=visible_threshold)
 def visible(mask):
  counts=mask.sum(axis=1);occupied=counts>=2;return {'visible_samples':int(mask.sum()),'depth_rows_ge2':int(occupied.sum()),'adjacent_occupied_depth_rows':bool((occupied[:-1]&occupied[1:]).any()),'peak_count_per_depth':int(counts.max())}
 return {'side':side,'band':[a,b],'raw_black_threshold':raw_threshold,'global_nearblack_cutoff':global_nearblack,'visibility_threshold':visible_threshold,'summary_sample_count':int(sy.size),'visibility_sample_count':int(ay.size),'original_absolute':summarize(sy,raw_threshold),'reference_residual':summarize(sr,24),'raw_visible':visible((ay>visible_threshold)|color),'reference_relative_visible':visible((ar>32)|color),'notes':'288-point summary and 64x256 visibility samples use frozen formulas. Coordinates are sampled relative to the selected band; guarded-band rows change sampling phase. Self-fitted residual is diagnostic only and never overrides raw visibility.'}
rows=[]
for name,(csvname,cropTop,cropBottom) in CASES.items():
 beta=next(csv.DictReader((root/csvname).open(encoding='utf-8-sig')));raw_threshold=int(beta['black_threshold']);global_nearblack=int(beta['global_cutoff']);vis_threshold=raw_threshold+8
 vals=np.fromfile(root/(name+'.yuv16'),dtype='<u2');p=W*H;Y=vals[:p].reshape(H,W).astype(np.int16);U=vals[p:2*p].reshape(H,W).astype(np.int16);V=vals[2*p:].reshape(H,W).astype(np.int16)
 for mode,topEnd,bottomStart in [('beta_nominal_excluded',cropTop,cropBottom),('8row_guarded',max(0,cropTop-8),min(H,cropBottom+8))]:
  for side,a,b in [('top',0,topEnd),('bottom',bottomStart,H)]:
   region=Y[a:b];res=region-ndimage.median_filter(region,size=17,mode='reflect');rows.append({'capture':name,'band_mode':mode,'beta_measured_bounds':[cropTop,cropBottom],'source_range':'Unknown (range field absent)','raw_black_reference_mode':beta['black_reference_mode'],'global_nearblack_cutoff':global_nearblack,'side':stats(Y,U,V,res,a,b,side,raw_threshold,vis_threshold)})
report={'protocol':'Production-shaped 288-sample edge summary plus 64x256 visibility sampling, matching frozen reference/protocol.txt formulas. Per-capture raw visibility uses IsCrediblyVisible threshold black_threshold+8; global_cutoff is recorded separately as global nearblack cutoff and is not the credibility threshold. Absolute summary threshold comes from unchanged beta extraction CSV; exterior median17 residual is diagnostic. Both nominal beta excluded band and 8-row-guarded band are reported because the latter changes sample phase. Raw source planes remain unchanged. This is not physical-aspect proof or crop authority.','records':rows}
(root/'reference-relative-real-captures.json').write_text(json.dumps(report,indent=2))
for x in rows:
 s=x['side'];print(x['capture'],x['band_mode'],s['side'],'thresholds',s['raw_black_threshold'],s['visibility_threshold'],'orig_pass',s['original_absolute']['summary_thresholds_pass'],'ref_pass',s['reference_residual']['summary_thresholds_pass'],'rawVisible',s['raw_visible'],'refVisible',s['reference_relative_visible'])
