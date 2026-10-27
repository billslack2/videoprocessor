from pathlib import Path
import csv,hashlib,json,importlib.util
import numpy as np
from scipy import ndimage
root=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression\synthetic'); W,H=3840,2160; top,btm=301,1857; cropT,cropB=293,1865
# Import only the exact frozen spline definitions; skip its native-capture execution loop.
sp=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile\spline_surface_experiment.py'); src=sp.read_text(); ns={};exec(compile(src.split('tiles=[',1)[0],str(sp),'exec'),ns)
SW,SH,TX,TY=ns['W'],ns['H'],ns['TX'],ns['TY']; assert (SW,SH,TX,TY)==(3840,293,64,32)
tiles=[(x0,y0,min(SW,x0+TX),min(SH,y0+TY),(x0//TX+y0//TY)%2) for y0 in range(0,SH,TY) for x0 in range(0,SW,TX)]
def surface_eval(Y):
 folds=[];held=np.zeros_like(Y,dtype=np.float32);components=[];textures=[];surface_ranges=[]
 raw_hp=np.abs(Y-ndimage.median_filter(Y,size=(17,17),mode='reflect'));raw_inner=raw_hp[8:-8,8:-8]
 for parity in (0,1):
  train=[t for t in tiles if t[4]==parity]; valid=[t for t in tiles if t[4]!=parity]; X=[];z=[]
  for x0,y0,x1,y1,p in train:
   X.append(ns['design_points']([(x0+x1-1)/2],[(y0+y1-1)/2])[0]); z.append(float(np.percentile(Y[y0:y1,x0:x1],25)))
  beta,obj=ns['fit_q'](np.stack(X),np.asarray(z)); rr_all=[];vmin=1e9;vmax=-1e9;foldcomps=[]
  for x0,y0,x1,y1,p in valid:
   pred=ns['predict_tile'](beta,x0,y0,x1,y1); rr=(Y[y0:y1,x0:x1]-pred).astype(np.float32); held[y0:y1,x0:x1]=rr;rr_all.append(rr.ravel());vmin=min(vmin,float(pred.min()));vmax=max(vmax,float(pred.max()))
   if rr.shape[0]>16 and rr.shape[1]>16:
    hp=np.abs(rr-ndimage.median_filter(rr,size=(17,17),mode='reflect'));inner=hp[8:-8,8:-8];textures.extend(inner.ravel().tolist())
   lab,n=ndimage.label(np.abs(rr)>=12,structure=np.array([[0,1,0],[1,1,1],[0,1,0]],np.uint8))
   for sl in ndimage.find_objects(lab):
    if sl is not None:
     area=int((lab[sl]>0).sum())
     if area>=4:foldcomps.append(area);components.append(area)
  rv=np.concatenate(rr_all); folds.append({'validation_parity':1-parity,'train_tiles':len(train),'validation_tiles':len(valid),'surface_range':[vmin,vmax],'abs_residual_percentiles':{str(q):float(np.percentile(np.abs(rv),q)) for q in (90,95,99,100)},'signed_p90_minus_p10':float(np.percentile(rv,90)-np.percentile(rv,10)),'fraction_abs_le4':float((np.abs(rv)<=4).mean()),'components_area_ge4_at_abs12':len(foldcomps),'largest_component':max(foldcomps,default=0)});surface_ranges.append([vmin,vmax])
 resid=np.asarray(held);absr=np.abs(resid);tex=np.asarray(textures);rawtx=raw_inner.ravel();return {'surface_range_union':[min(x[0] for x in surface_ranges),max(x[1] for x in surface_ranges)],'abs_residual_percentiles':{str(q):float(np.percentile(absr,q)) for q in (90,95,99,100)},'signed_residual_percentiles':{str(q):float(np.percentile(resid,q)) for q in (10,50,90)},'signed_p90_minus_p10':float(np.percentile(resid,90)-np.percentile(resid,10)),'fraction_abs_le4':float((absr<=4).mean()),'p90_le24':bool(np.percentile(absr,90)<=24),'spread_le24':bool(np.percentile(resid,90)-np.percentile(resid,10)<=24),'constant_p95_le4':bool(np.percentile(absr,95)<=4),'constant_p99_le12':bool(np.percentile(absr,99)<=12),'all_pm4':bool(np.all(absr<=4)),'heldout_local_texture_abs17_p95_p99':[float(np.percentile(tex,95)),float(np.percentile(tex,99))],'heldout_texture_le8':[bool(np.percentile(tex,95)<=8),bool(np.percentile(tex,99)<=8)],'raw_texture_abs17_p95_p99':[float(np.percentile(rawtx,95)),float(np.percentile(rawtx,99))],'heldout_abs12_component_count':len(components),'heldout_abs12_component_largest':max(components,default=0),'folds':folds}
def edge_scan(Y,U,V,side):
 xs=np.linspace(8,W-9,96).astype(int);rows=range(0,H//2) if side=='top' else range(H-2,H//2-1,-1)
 for r in rows:
  iy,oy=(r+1,r) if side=='top' else (r,r+1);dy=Y[iy,xs]-Y[oy,xs] if side=='top' else Y[iy,xs]-Y[oy,xs];dc=np.maximum(abs(U[iy,xs]-U[oy,xs]),abs(V[iy,xs]-V[oy,xs]));ok=(dy>=20)&(dc>=16);q=[float(ok[i*24:(i+1)*24].mean()) for i in range(4)]
  if min(q)>=.70:
   lo=max(0,r-4);hi=min(H-2,r+4);best=np.full(W,-1,np.int16);bd=np.full(W,-1,np.int16)
   for rr in range(lo,hi+1):
    a,b=(rr+1,rr) if side=='top' else (rr,rr+1);d=Y[a,:]-Y[b,:];c=np.maximum(abs(U[a,:]-U[b,:]),abs(V[a,:]-V[b,:]));d=np.where((d>=20)&(c>=16),d,-1);take=d>bd;best[take]=rr;bd[take]=d[take]
   sup=best>=0;err=np.abs(best[sup]-r);return {'outside_row':int(r if side=='top' else r+1),'inside_row':int(iy if side=='top' else r),'quartile_support':q,'full_column_support':float(sup.mean()),'full_column_error_p95':float(np.percentile(err,95)) if err.size else None,'line_pass':bool(sup.mean()>=.95 and err.size and np.percentile(err,95)<=2),'median_dY':float(np.median(dy)),'median_dUV':float(np.median(dc))}
 return None
def profile_ext(Y,U,V,side):
 a,b=(0,cropT) if side=='top' else (cropB,H);A=Y[a:b];AU=U[a:b];AV=V[a:b];med=ndimage.median_filter(A,size=17,mode='reflect');ry=np.abs(A.astype(np.float32)-med.astype(np.float32));ru=np.abs(AU.astype(np.float32)-ndimage.median_filter(AU,size=17,mode='reflect'));rv=np.abs(AV.astype(np.float32)-ndimage.median_filter(AV,size=17,mode='reflect')); neutral=float(((abs(AU-512)<=16)&(abs(AV-512)<=16)).mean());p95,p99=np.percentile(ry,[95,99]);chp99=max(np.percentile(ru,99),np.percentile(rv,99))
 mask=ry>=12;lab,n=ndimage.label(mask,structure=np.ones((3,3),np.uint8));comp=[]
 for sl in ndimage.find_objects(lab):
  if sl is None:continue
  sub=lab[sl]>0;area=int(sub.sum());h=sl[0].stop-sl[0].start;w=sl[1].stop-sl[1].start;comp.append({'area':area,'width':w,'height':h})
 veto=[c for c in comp if c['area']>=200 or(c['width']>=24 and c['height']>=12)];bins=[]
 # bins stay on global raster origin, intersected with this candidate exterior band.
 for gy in range((a//32)*32,b,32):
  for x0 in range(0,W,64):
   y0=max(a,gy);y1=min(b,gy+32);occ=float(mask[y0-a:y1-a,x0:min(W,x0+64)].mean())
   if occ>=.08:bins.append({'x':x0,'y0':y0,'y1':y1,'occupancy':occ})
 tile=32;grid=np.array([[np.median(A[y0:min(y0+tile,A.shape[0]),x0:min(x0+tile,W)]) for x0 in range(0,W,tile)] for y0 in range(0,A.shape[0],tile)]);steps=np.r_[abs(np.diff(grid,axis=0)).ravel(),abs(np.diff(grid,axis=1)).ravel()];step95=float(np.percentile(steps,95));stepmax=float(steps.max())
 basepass=neutral>=.98 and p95<=4 and p99<=12 and step95<=8 and stepmax<=24 and chp99<=8; morph=bool(veto or bins)
 return {'band':[a,b],'neutral_fraction':neutral,'residual_y_p95_p99':[float(p95),float(p99)],'residual_chroma_p99':float(chp99),'tile_step_p95_max':[step95,stepmax],'component_count_8conn':len(comp),'morph_large_components':veto,'bin_vetoes':bins,'base_admission_pass':bool(basepass),'morphology_veto':morph,'route_profile_pass_this_edge':bool(basepass and not morph)}
# Build results per case from exact bytes after Python and C++ all-pixel validation.
names=[r['name'] for r in json.loads((root/'manifest.json').read_text())['controls']]
beta={r['kind']:r for r in csv.DictReader((root/'beta-results.csv').open(encoding='utf-8-sig'))} if (root/'beta-results.csv').exists() else {}
records=[]
for name in names:
 raw=(root/(name+'.p210')).read_bytes();v=np.frombuffer(raw,dtype='<u2');P=W*H;Y=(v[:P].reshape(H,W)>>6).astype(np.int16);uv=(v[P:].reshape(H,W//2,2)>>6).astype(np.int16);U=np.repeat(uv[:,:,0],2,axis=1).astype(np.int16);V=np.repeat(uv[:,:,1],2,axis=1).astype(np.int16)
 # Rerun one frozen independent spline model on exactly the protocol top band, without altering its basis/partition.
 sfit=surface_eval(Y[:293,:].astype(float))
 # Frozen independent edge and exterior/morphology diagnostics.
 edges={side:edge_scan(Y,U,V,side) for side in ('top','bottom')};ext={side:profile_ext(Y,U,V,side) for side in ('top','bottom')}
 records.append({'name':name,'edge_evidence':edges,'frozen_profile_morphology':ext,'spline_model_heldout':sfit,'beta':beta.get(name)})
 report={'heldout_generator_protocol_sha256':hashlib.sha256((root/'heldout-protocol.txt').read_bytes()).hexdigest(),'generator_sha256':hashlib.sha256((root/'generate_controls.py').read_bytes()).hexdigest(),'frozen_morphology_protocol_sha256':hashlib.sha256(Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile\protocol.txt').read_bytes()).hexdigest(),'fixed_spline_protocol_sha256':hashlib.sha256(Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile\spline-surface-protocol.txt').read_bytes()).hexdigest(),'spline_implementation_sha256':hashlib.sha256(sp.read_bytes()).hexdigest(),'cases':records}
(root/'classifier-results.json').write_text(json.dumps(report,indent=2));
for r in records:
 print(r['name'],'edge',[(k,v['outside_row'] if v else None,v['line_pass'] if v else None) for k,v in r['edge_evidence'].items()],'morph',[(k,v['base_admission_pass'],v['morphology_veto']) for k,v in r['frozen_profile_morphology'].items()],'spline',r['spline_model_heldout']['abs_residual_percentiles']['90'],r['spline_model_heldout']['abs_residual_percentiles']['99'],r['spline_model_heldout']['fraction_abs_le4'],'beta',r['beta']['class'] if r['beta'] else None)

