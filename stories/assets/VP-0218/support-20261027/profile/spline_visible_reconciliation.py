from pathlib import Path
import csv,json,hashlib
import numpy as np
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1');ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay');OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile')
H,W,DEPTH=293,3840,293

def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();P=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,P.shape[1],6),np.uint16);U=np.empty((h,P.shape[1],3),np.uint16);V=np.empty_like(U)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(P[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(P[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w],np.repeat(U.reshape(h,-1),2,axis=1)[:,:w],np.repeat(V.reshape(h,-1),2,axis=1)[:,:w]
def vis_stats(mask):
 counts=mask.sum(axis=1);rows=counts>=2
 return {'visible_samples':int(mask.sum()),'occupied_depth_rows_ge2':int(rows.sum()),'occupied_row_indices':[int(i) for i in np.where(rows)[0]],'adjacent_occupied_depth_rows':bool((rows[:-1]&rows[1:]).any()),'peak_count_per_depth':int(counts.max()),'counts_by_nonzero_row':[{'row_index':int(i),'source_y':int(((2*i+1)*DEPTH)//128),'count':int(c)} for i,c in enumerate(counts) if c>0]}
coord=json.loads((OUT/'spline-visible-probe-coordinates.json').read_text());rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));unique={}
for row in rows:unique.setdefault(row['SHA256'],row['File'])
arrays={};records=[]
for idx,(sha,name) in enumerate(unique.items()):
 Y,U,V=decode(ROOT/name);Y=Y[:DEPTH,:];U=U[:DEPTH,:];V=V[:DEPTH,:];vy=((2*np.arange(64)+1)*DEPTH)//128;vx=((2*np.arange(256)+1)*W)//512;ys=Y[np.ix_(vy,vx)];us=U[np.ix_(vy,vx)];vs=V[np.ix_(vy,vx)];color=(np.maximum(abs(us.astype(int)-512),abs(vs.astype(int)-512))>=64)&(ys>=96);rawmask=(ys>96)|color
 rec=coord['records'][idx];rmask=np.zeros((64,256),bool)
 for p in rec['all_27_or_fewer_probe_points']:rmask[p['probe_row_index'],p['probe_col_index']]=True
 inter=rawmask&rmask;phantom=rmask&~rawmask;raw_only=rawmask&~rmask
 arrays[f'f{idx+1}_raw']=rawmask;arrays[f'f{idx+1}_residual']=rmask;arrays[f'f{idx+1}_intersection']=inter
 points=[]
 for p in rec['all_27_or_fewer_probe_points']:
  if p['raw_color_witness'] or p['raw_Y']>96:
   points.append({k:p[k] for k in ('x','y','raw_Y','spline_prediction_Y','heldout_residual_R','local17_signed_residual','local17_abs_residual_ge12_component_id','local17_component_bbox_xyxy','raw_color_witness')})
 phantPts=[p for p in rec['all_27_or_fewer_probe_points'] if not p['raw_color_witness'] and p['raw_Y']<=96]
 records.append({'file':name,'sha256':sha,'raw_visible':vis_stats(rawmask),'spline_relative_visible':vis_stats(rmask),'reconciled_raw_and_residual_intersection':vis_stats(inter),'raw_only':vis_stats(raw_only),'model_only_phantom':vis_stats(phantom),'phantom_rawY_min_median_max':[float(min(p['raw_Y'] for p in phantPts)),float(np.median([p['raw_Y'] for p in phantPts])),float(max(p['raw_Y'] for p in phantPts))] if phantPts else None,'phantom_model_prediction_below64_count':sum(p['spline_prediction_Y']<64 for p in phantPts),'reconciled_localized_details':points})
npz=OUT/'spline-visible-probe-masks.npz';np.savez_compressed(npz,**arrays)
report={'definition':'At the original 64x256 witness coordinates, rawVisible=(Y>96) OR original raw color mask; splineRelativeVisible=(heldout R>32) OR same original raw color mask; reconciled=AND. For luma-only samples this is Y>max(96, fittedY+32). No threshold changes. The compressed masks are probe-grid masks, not full-resolution source masks.','coordinate_formula_source_sha256':hashlib.sha256((OUT/'spline_production_guard_followup.py').read_bytes()).hexdigest(),'probe_masks_npz':npz.name,'probe_masks_sha256':hashlib.sha256(npz.read_bytes()).hexdigest(),'records':records,'interpretation':['The original raw witness is retained separately and is not replaced by model-only visibility.','The model-only mask can flag low source luma where fitted profile is lower; intersection prevents this model-created foreground.','Intersection count/row gate is a diagnostic result, not a production approval.']}
(OUT/'spline-visible-probe-reconciliation.json').write_text(json.dumps(report,indent=2));print(json.dumps({'probe_masks_sha256':report['probe_masks_sha256'],'records':[{'file':r['file'],'raw':r['raw_visible'],'relative':r['spline_relative_visible'],'intersection':r['reconciled_raw_and_residual_intersection'],'phantom':r['model_only_phantom'],'phantomY':r['phantom_rawY_min_median_max'],'model_below64':r['phantom_model_prediction_below64_count'],'localized':r['reconciled_localized_details']} for r in records]},indent=2))
