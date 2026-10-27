from pathlib import Path
import csv,hashlib,json,time
import numpy as np
from scipy import ndimage
from skimage.morphology import skeletonize
from PIL import Image,ImageDraw
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1'); ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay'); OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile'); TOP=293

def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();W=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,W.shape[1],6),np.uint16)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(W[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(np.int16)
def get_comps(res,th,conn):
 st=ndimage.generate_binary_structure(2,1 if conn==4 else 2); lab,n=ndimage.label(res>=th,structure=st);cs=[]
 for k,sl in enumerate(ndimage.find_objects(lab),1):
  if sl is None:continue
  a=lab[sl]==k;area=int(a.sum())
  if area<16:continue
  yy,xx=np.nonzero(a);ys,xs=sl;vals=res[sl][a];h=int(ys.stop-ys.start);w=int(xs.stop-xs.start);per=int(a.sum()-ndimage.binary_erosion(a,structure=st,border_value=0).sum());sk=skeletonize(a);nn=ndimage.convolve(sk.astype(np.uint8),np.ones((3,3),np.uint8),mode='constant')-sk.astype(np.uint8)
  cs.append({'area':area,'bbox':[int(xs.start),int(ys.start),int(xs.stop),int(ys.stop)],'w':w,'h':h,'aspect':round(w/max(1,h),3),'fill':round(area/(w*h),4),'perimeter_pixels':per,'compactness':round(4*np.pi*area/(per*per),4),'peak':int(vals.max()),'p95':round(float(np.percentile(vals,95)),2),'mean':round(float(vals.mean()),2),'skeleton_endpoints':int(np.count_nonzero(sk&(nn==1))),'skeleton_branches':int(np.count_nonzero(sk&(nn>=3)))})
 return sorted(cs,key=lambda c:(-c['area'],c['bbox'][1],c['bbox'][0]))

def summary(a):return [round(float(v),3) for v in np.percentile(a,[50,90,95,99,99.9,100])]
rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig'))); uniq={}
for r in rows:uniq.setdefault(r['SHA256'],r['File'])
records=[]; frame_masks=[]; frame_17=[]
for digest,name in uniq.items():
 y=decode(ROOT/name)[:TOP,:]; med=ndimage.median_filter(y,size=(17,17),mode='reflect'); signed=y.astype(np.float32)-med;res=np.abs(signed); cs=get_comps(res,12,4); c8=get_comps(res,12,8);frame_masks.append((res>=12)); frame_17.append(res)
 records.append({'file':name,'sha256':digest,'raw_y_percentiles':summary(y),'signed_resid17_percentiles':summary(signed),'abs_resid17_percentiles':summary(res),'components_12_4conn':cs,'components_12_8conn':c8,'component_count_ge16_by_threshold_and_connectivity':{f'{th}_conn{conn}':len(get_comps(res,th,conn)) for th in (12,24,48) for conn in (4,8)}})
# Pairwise spatial threshold-mask agreement, plus scale comparisons only on unique frame #1.
pair=[]
for i in range(len(frame_masks)):
 for j in range(i+1,len(frame_masks)):
  a,b=frame_masks[i],frame_masks[j]; inter=int((a&b).sum());union=int((a|b).sum());pair.append({'frame_a':records[i]['file'],'frame_b':records[j]['file'],'mask12_exact_fraction':float((a==b).mean()),'mask12_iou':inter/max(1,union),'component_bbox_signature_exact':records[i]['components_12_4conn']==records[j]['components_12_4conn']})
# Scale comparison for first payload: exact 17 square baseline; separable larger median approximation.
y=decode(ROOT/records[0]['file'])[:TOP,:]; med17=ndimage.median_filter(y,size=(17,17),mode='reflect');r17=np.abs(y.astype(np.float32)-med17.astype(np.float32));scale=[]
for k in (65,129,257):
 hmed=ndimage.median_filter(y,size=(1,k),mode='reflect');base=ndimage.median_filter(hmed,size=(k,1),mode='reflect');r=np.abs(y.astype(np.float32)-base.astype(np.float32))
 for th in (12,24,48):
  a=r17>=th;b=r>=th;scale.append({'reference_scale':k,'threshold':th,'mask17_fraction':float(a.mean()),'mask_scale_fraction':float(b.mean()),'mask_iou':float((a&b).sum()/max(1,(a|b).sum())),'mask17_covered_by_scale':float((a&b).sum()/max(1,a.sum())),'scale_covered_by_17':float((a&b).sum()/max(1,b.sum()))})
# 64x32 raster-origin occupancy and nearest-neighbor spacing of component centroids.
cs=records[0]['components_12_4conn'];centers=np.array([[(c['bbox'][0]+c['bbox'][2])/2,(c['bbox'][1]+c['bbox'][3])/2] for c in cs]);d=np.sqrt(((centers[:,None,:]-centers[None,:,:])**2).sum(2));np.fill_diagonal(d,np.inf)
bins=[]
for yy in range(0,TOP,32):
 for xx in range(0,3840,64):
  count=int(((r17>=12)[yy:min(yy+32,TOP),xx:xx+64]).sum())
  if count:bins.append({'x':xx,'y':yy,'active_pixels':count,'occupancy':count/(min(32,TOP-yy)*64)})
report={'protocol_sha256':hashlib.sha256((OUT/'protocol.txt').read_bytes()).hexdigest(),'implementation_note_sha256':hashlib.sha256((OUT/'implementation-note.txt').read_bytes()).hexdigest(),'records':records,'cross_frame_mask_agreement':pair,'cross_scale_mask_iou_first_unique_payload':scale,'spatial_summary_first_unique_payload':{'centroid_covariance':np.cov(centers.T).tolist(),'nearest_neighbor_distance_px_percentiles':summary(d.min(axis=1)),'nonzero_64x32_bins':len(bins),'max_64x32_bin':max(bins,key=lambda b:b['active_pixels']),'occupied_bins':bins},'interpretation_limits':['Four payloads are four frames from one ad, not four independent scenes.','Stable coordinate masks establish spatial repeatability in this clip, not that components are benign texture or safe to crop.','A subpixel-exact semantic distinction between texture and tiny logo/star is unavailable from this sample alone.']}
(OUT/'native-component-structure.json').write_text(json.dumps(report,indent=2))
# Detail image uses native Y values mapped to display range; red rectangles mark 17x17 residual components >=12 (4-connectivity).
lo,hi=np.percentile(y,[1,99]); disp=np.clip((y-lo)*255/max(1,hi-lo),0,255).astype(np.uint8); im=Image.fromarray(disp,'L').convert('RGB'); draw=ImageDraw.Draw(im)
for i,c in enumerate(records[0]['components_12_4conn']):
 x0,y0,x1,y1=c['bbox'];draw.rectangle((x0,y0,x1-1,y1-1),outline=(255,40,40),width=1);draw.text((x0,y0),str(i+1),fill=(255,255,0))
im.resize((1920,147),Image.Resampling.NEAREST).save(OUT/'native-top-components.png')
print(json.dumps({'protocol_sha256':report['protocol_sha256'],'records':[{'file':r['file'],'abs_resid17':r['abs_resid17_percentiles'],'n12c4':len(r['components_12_4conn']),'n12c8':len(r['components_12_8conn']),'n24c4':r['component_count_ge16_by_threshold_and_connectivity']['24_conn4'],'n48c4':r['component_count_ge16_by_threshold_and_connectivity']['48_conn4']} for r in records],'cross_frame':pair,'scales':scale,'spatial':report['spatial_summary_first_unique_payload']},indent=2))
