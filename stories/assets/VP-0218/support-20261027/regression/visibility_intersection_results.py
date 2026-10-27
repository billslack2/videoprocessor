from pathlib import Path
import csv,json
import numpy as np
from scipy import ndimage
W,H=3840,2160; out=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression')
def vec_from_yuv16(name,folder):
 x=np.fromfile(folder/(name+'.yuv16'),dtype='<u2');p=W*H;return x[:p].reshape(H,W).astype(np.int16),x[p:2*p].reshape(H,W).astype(np.int16),x[2*p:].reshape(H,W).astype(np.int16)
def vec_from_p210(name,folder):
 x=np.fromfile(folder/(name+'.p210'),dtype='<u2');p=W*H;y=(x[:p].reshape(H,W)>>6).astype(np.int16);uv=(x[p:].reshape(H,W//2,2)>>6).astype(np.int16);u=np.repeat(uv[:,:,0],2,axis=1).astype(np.int16);v=np.repeat(uv[:,:,1],2,axis=1).astype(np.int16);return y,u,v
def sample(Y,U,V,baseY,vis,a,b,side):
 d=b-a;n=min(64,d);ys=((2*np.arange(n)+1)*d)//(2*n);ys=ys if side=='top' else H-1-ys;xs=((2*np.arange(256)+1)*W)//512
 y=Y[np.ix_(ys,xs)];u=U[np.ix_(ys,xs)];v=V[np.ix_(ys,xs)];res=(Y-baseY)[np.ix_(ys,xs)];color=(np.maximum(abs(u-512),abs(v-512))>=64)&(y>=vis)
 raw=(y>vis)|color;relative=(res>32)|color;intersection=((y>vis)&(res>32))|color
 def m(mask):
  cnt=mask.sum(axis=1); rows=cnt>=2;return {'samples':int(mask.sum()),'rows_ge2':int(rows.sum()),'adjacent_rows':bool((rows[:-1]&rows[1:]).any()),'peak_per_row':int(cnt.max())}
 return {'raw_visible':m(raw),'reference_visible':m(relative),'intersection_visible':m(intersection),'effective_luma_cutoff_example':'max(raw_black_threshold+8, local_median17+32); chroma preserved separately'}
def process(name,Y,U,V,betarow,nominal,guarded,category):
 global_nearblack=int(betarow['global_cutoff']);black=int(betarow['black_threshold']);vis=black+8;rows=[]
 for mode,(top,bottom) in [('nominal',nominal),('guarded',guarded)]:
  for side,a,b in [('top',0,top),('bottom',bottom,H)]:
   A=Y[a:b];base=ndimage.median_filter(A,size=17,mode='reflect');model=Y.copy();model[a:b]=base
   rows.append({'capture':name,'category':category,'band_mode':mode,'side':side,'raw_black_threshold':black,'global_nearblack_cutoff':global_nearblack,'credibility_visible_threshold':vis,'metrics':sample(Y,U,V,model,vis,a,b,side)})
 return rows
records=[]; realDir=out
realCSV={'city-lights':'city-lights.csv','thai-top-card':'thai-top-card.csv','latin-three-lines':'latin-three-lines.csv'}
for name in ('city-lights','thai-top-card','latin-three-lines'):
 br=next(csv.DictReader((out/realCSV[name]).open(encoding='utf-8-sig')))
 Y,U,V=vec_from_yuv16(name,realDir);top=int(br['trusted_top']);bottom=int(br['trusted_bottom']);records+=process(name,Y,U,V,br,(top,bottom),(max(0,top-8),min(H,bottom+8)),'real_capture')
sdir=out/'synthetic'; beta={r['kind']:r for r in csv.DictReader((sdir/'beta-results.csv').open(encoding='utf-8-sig'))}
manifest=json.loads((sdir/'manifest.json').read_text())
for c in manifest['controls']:
 name=c['name'];br=beta[name];Y,U,V=vec_from_p210(name,sdir);records+=process(name,Y,U,V,br,(301,1857),(293,1865),'synthetic_control')
(out/'visibility-intersection-results.json').write_text(json.dumps({'protocol':'All masks use exact production 64x256 sampled visibility locations. IsCrediblyVisible threshold is raw black_threshold+8; global_cutoff is separately retained as global nearblack metadata and is not the visibility threshold. Reference is local reflected median17 of selected exterior band. Raw is Y>credibility threshold or existing chroma condition. Relative is R>32 or same chroma condition. Intersection is (Y>credibility threshold AND R>32) OR chroma; equivalent Y>max(black_threshold+8, local_reference+32), while retaining color evidence. Report nominal and 8-row-guarded sample phases separately. This diagnostic does not grant crop authority.','records':records},indent=2))
for r in records:
 if r['side']=='top' and r['band_mode'] in ('nominal','guarded'):
  m=r['metrics'];print(r['capture'],r['category'],r['band_mode'],'raw/ref/intersection',m['raw_visible']['samples'],m['reference_visible']['samples'],m['intersection_visible']['samples'],'intersect rows/adj',m['intersection_visible']['rows_ge2'],m['intersection_visible']['adjacent_rows'])

