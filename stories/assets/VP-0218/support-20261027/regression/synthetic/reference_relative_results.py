from pathlib import Path
import csv,json
import numpy as np
from scipy import ndimage
root=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression\synthetic');W,H=3840,2160
beta={r['kind']:r for r in csv.DictReader((root/'beta-results.csv').open(encoding='utf-8-sig'))}
def pct(a,q):
 a=np.sort(np.asarray(a).ravel());return float(a[int(q*(len(a)-1))])
def summarize(vals,texture,neutral,contrast,threshold):
 black=vals<=threshold;continuity=float((black.sum(axis=1)>=44).mean());out={'black_fraction':float(black.mean()),'p90':pct(vals,.9),'dispersion_p90_p10':pct(vals,.9)-pct(vals,.1),'texture':texture,'neutral':neutral,'continuity':continuity,'edge_contrast':contrast};out['existing_summary_pass']=bool(out['black_fraction']>=.95 and out['p90']<=threshold and out['dispersion_p90_p10']<=24 and texture<=8 and neutral>=.90 and continuity>=.99);return out
def visible(mask):
 counts=mask.sum(axis=1);occupied=counts>=2;return {'samples':int(mask.sum()),'rows_ge2':int(occupied.sum()),'adjacent_rows':bool((occupied[:-1]&occupied[1:]).any()),'peak_per_row':int(counts.max())}
def measure(Y,U,V,R,a,b,side,black,vis):
 depth=b-a;yy=((2*np.arange(6)+1)*depth)//12;yy=yy if side=='top' else H-1-yy;xx=((2*np.arange(48)+1)*W)//96;sy=Y[np.ix_(yy,xx)];su=U[np.ix_(yy,xx)];sv=V[np.ix_(yy,xx)];sr=R[np.ix_(yy-a,xx)];texture=float(np.abs(np.diff(sy.astype(float),axis=1)).mean());neutral=float(((abs(su-512)<=32)&(abs(sv-512)<=32)).mean());boundary=b if side=='top' else a;inside=boundary+2 if side=='top' else boundary-3;contrast=float(Y[inside,xx].mean()-sy.mean())
 d=min(64,depth);vy=((2*np.arange(d)+1)*depth)//(2*d);vy=vy if side=='top' else H-1-vy;vx=((2*np.arange(256)+1)*W)//512;ay=Y[np.ix_(vy,vx)];au=U[np.ix_(vy,vx)];av=V[np.ix_(vy,vx)];ar=R[np.ix_(vy-a,vx)];color=(np.maximum(abs(au-512),abs(av-512))>=64)&(ay>=vis)
 return {'band':[a,b],'sample_count':288,'visibility_grid':[d,256],'absolute':summarize(sy,texture,neutral,contrast,black),'reference_residual':summarize(sr,texture,neutral,contrast,24),'raw_visible':visible((ay>vis)|color),'reference_relative_visible':visible((ar>32)|color)}
records=[]
for name,b in beta.items():
 vals=np.fromfile(root/(name+'.p210'),dtype='<u2');p=W*H;Y=(vals[:p].reshape(H,W)>>6).astype(np.int16);uv=(vals[p:].reshape(H,W//2,2)>>6).astype(np.int16);U=np.repeat(uv[:,:,0],2,axis=1).astype(np.int16);V=np.repeat(uv[:,:,1],2,axis=1).astype(np.int16);black=int(b['black_threshold']);global_nearblack=int(b['global_cutoff']);vis=black+8
 modes=[('nominal_fixture_edges',0,301,1857,H),('guarded_candidate',0,293,1865,H)]
 for mode,ta,tb,ba,be in modes:
  for side,a,z in [('top',ta,tb),('bottom',ba,be)]:
   raw=Y[a:z];res=raw-ndimage.median_filter(raw,size=17,mode='reflect');records.append({'name':name,'band_mode':mode,'side':side,'beta_black_threshold':black,'beta_global_nearblack_cutoff':global_nearblack,'beta_credibility_visible_threshold':vis,'measurement':measure(Y,U,V,res,a,z,side,black,vis)})
report={'protocol':'Exact production-shaped formulas copied from frozen reference/compare_reference.py, applied to these full-resolution P210 controls. IsCrediblyVisible uses measured beta raw black threshold+8; global_cutoff is recorded separately as global nearblack cutoff metadata and is not the credibility threshold. Separate nominal fixture outer bands and guarded candidate [0,293)/[1865,2160); sampling phase differs. Median17 residual is diagnostic only; raw samples remain unchanged. No crop authority.','records':records}
(root/'reference-relative-results.json').write_text(json.dumps(report,indent=2))
for r in records:
 m=r['measurement'];print(r['name'],r['band_mode'],r['side'],'summary_abs/ref',m['absolute']['existing_summary_pass'],m['reference_residual']['existing_summary_pass'],'visible raw/ref',m['raw_visible']['samples'],m['raw_visible']['rows_ge2'],m['raw_visible']['adjacent_rows'],m['reference_relative_visible']['samples'],m['reference_relative_visible']['rows_ge2'],m['reference_relative_visible']['adjacent_rows'])
