from pathlib import Path
import json
import numpy as np
from scipy import ndimage
root=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression')
CASES={'city-lights':(66,2092),'thai-top-card':(274,1884),'latin-three-lines':(274,1884)}
W,H=3840,2160
report={'protocol':'Frozen morphology predicate only: abs(Y-median17)>=12; reject if any 8-connected component area>=200 OR bbox width>=24 AND height>=12 OR any raster-origin 64x32 bin occupancy>=8%. Beta bounds are used as candidate edges with an 8-row outward fringe; no sidecar bounds used. This is a diagnostic on existing pixels, not full profile admission/crop authority.','cases':[]}
for name,(top,bottom) in CASES.items():
 v=np.fromfile(root/(name+'.yuv16'),dtype='<u2');plane=W*H; Y=v[:plane].reshape(H,W).astype(np.int16);U=v[plane:2*plane].reshape(H,W).astype(np.int16);V=v[2*plane:].reshape(H,W).astype(np.int16)
 sides=[]
 for side,lo,hi in [('top',0,max(0,top-8)),('bottom',min(H,bottom+8),H)]:
  a=Y[lo:hi,:];u=U[lo:hi,:];vv=V[lo:hi,:]
  if a.size==0: continue
  med=ndimage.median_filter(a,size=17,mode='reflect');r=np.abs(a.astype(np.int16)-med.astype(np.int16));mask=r>=12
  lab,n=ndimage.label(mask,structure=np.ones((3,3),np.uint8));objs=ndimage.find_objects(lab);stats=[]
  for i,sl in enumerate(objs,1):
   if sl is None:continue
   area=int((lab[sl]==i).sum());y0,y1=sl[0].start,sl[0].stop;x0,x1=sl[1].start,sl[1].stop
   stats.append({'area':area,'bbox':[int(x0),int(y0+lo),int(x1),int(y1+lo)],'width':int(x1-x0),'height':int(y1-y0)})
  for y0 in range((lo//32)*32,hi,32):
   for x0 in range(0,W,64):
    ya=max(lo,y0);yb=min(hi,y0+32);xb=min(W,x0+64)
    if yb<=ya:continue
    occ=float(mask[ya-lo:yb-lo,x0:xb].mean())
    if occ>=.08: stats.append({'bin_occ_veto':True,'bin':[x0,ya,x0+64,yb],'occupancy':occ})
  comps=[s for s in stats if 'area' in s];large=[s for s in comps if s['area']>=200 or (s['width']>=24 and s['height']>=12)]
  bins=[s for s in stats if 'bin_occ_veto' in s]
  neutral=float(((np.abs(u-512)<=16)&(np.abs(vv-512)<=16)).mean())
  sides.append({'side':side,'band_y':[lo,hi],'pixels':int(a.size),'neutral_fraction':neutral,'residual_percentiles':[float(x) for x in np.percentile(r,[50,95,99,100])],'mask_pixels':int(mask.sum()),'component_count_8conn':len(comps),'large_component_vetoes':large,'bin_occupancy_vetoes':bins,'morphology_reject':bool(large or bins),'largest_components':sorted(comps,key=lambda x:x['area'],reverse=True)[:10]})
 report['cases'].append({'name':name,'beta_candidate_edges':[top,bottom],'sides':sides})
(root/'frozen-morphology-real-captures.json').write_text(json.dumps(report,indent=2))
for c in report['cases']:
 print(c['name'],[(s['side'],s['morphology_reject'],s['component_count_8conn'],len(s['large_component_vetoes']),len(s['bin_occupancy_vetoes']),s['neutral_fraction']) for s in c['sides']])
