from pathlib import Path
import csv, hashlib, json, time
import numpy as np
from scipy import ndimage
from skimage.morphology import skeletonize
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1')
ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay')
OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile')
TOP=293

def decode(path):
    meta=Path(path).with_suffix('.txt').read_text().splitlines()[0].split(); w,h,stride=map(int,meta[:3]); raw=Path(path).read_bytes()
    W=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4)
    Y=np.empty((h,W.shape[1],6),np.uint16); U=np.empty((h,W.shape[1],3),np.uint16); V=np.empty_like(U)
    for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))): Y[:,:,i]=(W[:,:,n]>>s)&1023
    for i,(n,s) in enumerate(((0,0),(1,10),(2,20))): U[:,:,i]=(W[:,:,n]>>s)&1023
    for i,(n,s) in enumerate(((0,20),(2,0),(3,10))): V[:,:,i]=(W[:,:,n]>>s)&1023
    return Y.reshape(h,-1)[:,:w].astype(np.int16),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16)

def components(res, threshold, conn):
    structure=ndimage.generate_binary_structure(2,1 if conn==4 else 2)
    lab,n=ndimage.label(res>=threshold,structure=structure); out=[]
    for k,sl in enumerate(ndimage.find_objects(lab),1):
        if sl is None: continue
        local=(lab[sl]==k); area=int(local.sum())
        if area<16: continue
        ys,xs=sl; yy,xx=np.nonzero(local); vals=res[sl][local]
        # Estimate boundary edge count and skeleton topology for this component.
        er=ndimage.binary_erosion(local,structure=structure,border_value=0); perimeter=int(local.sum()-er.sum())
        sk=skeletonize(local)
        neighbors=ndimage.convolve(sk.astype(np.uint8),np.ones((3,3),np.uint8),mode='constant')-sk.astype(np.uint8)
        ends=int(np.count_nonzero(sk & (neighbors==1))); branches=int(np.count_nonzero(sk & (neighbors>=3)))
        fill=area/max(1,(ys.stop-ys.start)*(xs.stop-xs.start))
        out.append({'area':area,'x0':int(xs.start),'x1':int(xs.stop),'y0':int(ys.start),'y1':int(ys.stop),'w':int(xs.stop-xs.start),'h':int(ys.stop-ys.start),'aspect':round((xs.stop-xs.start)/max(1,ys.stop-ys.start),3),'fill':round(float(fill),4),'perimeter4_est':perimeter,'compactness':round(float(4*np.pi*area/(perimeter*perimeter)),4),'peak':int(vals.max()),'p95':round(float(np.percentile(vals,95)),2),'mean':round(float(vals.mean()),2),'skeleton_endpoints':ends,'skeleton_branches':branches})
    return sorted(out,key=lambda c:(-c['area'],c['y0'],c['x0']))

def percentiles(a): return [round(float(x),3) for x in np.percentile(a,[50,90,95,99,99.9,100])]
def profile_summary(y):
    # fixed prior diagnostics for model/background-vs-structure separation
    median17=ndimage.median_filter(y,size=(17,17),mode='reflect')
    residual=np.abs(y.astype(np.float32)-median17.astype(np.float32))
    tile=[]
    for bs in (32,64,128):
        vals=np.array([[np.median(y[j:min(j+bs,y.shape[0]),i:min(i+bs,y.shape[1])]) for i in range(0,y.shape[1],bs)] for j in range(0,y.shape[0],bs)])
        d=np.abs(np.diff(vals,axis=0)).ravel(); dx=np.abs(np.diff(vals,axis=1)).ravel()
        tile.append({'size':bs,'median_range':[float(vals.min()),float(vals.max())],'step_p95':float(np.percentile(np.r_[d,dx],95)) if d.size+dx.size else 0,'step_max':float(max(d.max(initial=0),dx.max(initial=0)))})
    arr=[]
    for th in (12,24,48):
        for conn in (4,8):
            cs=components(residual,th,conn)
            arr.append({'threshold':th,'connectivity':conn,'count_ge16':len(cs),'area_summary':([min(c['area'] for c in cs),float(np.median([c['area'] for c in cs])),float(np.percentile([c['area'] for c in cs],95)),max(c['area'] for c in cs)] if cs else [0,0,0,0]),'max_bbox':[max((c['w'] for c in cs),default=0),max((c['h'] for c in cs),default=0)],'max_peak':max((c['peak'] for c in cs),default=0),'components':cs if th==12 and conn==8 else None})
    return median17,residual,{'y_percentiles':percentiles(y),'local_residual_abs_percentiles':percentiles(residual),'components':arr,'tile_profiles':tile}

def low_scales(y):
    res={}
    for k in (65,129,257):
        # Separable reflected medians are fixed, scalable spatial baselines; these are not exact 2D square medians.
        hmed=ndimage.median_filter(y,size=(1,k),mode='reflect')
        base=ndimage.median_filter(hmed,size=(k,1),mode='reflect')
        r=np.abs(y.astype(np.float32)-base.astype(np.float32))
        res[str(k)]={'absolute_residual_percentiles':percentiles(r),'signed_residual_percentiles':percentiles(y.astype(np.float32)-base.astype(np.float32)),'components_12_conn8':components(r,12,8)}
    return res

def spatial(cs):
    if not cs:return {}
    centers=np.array([[(c['x0']+c['x1'])/2,(c['y0']+c['y1'])/2] for c in cs]); d=np.sqrt(((centers[:,None,:]-centers[None,:,:])**2).sum(2)); d[d==0]=np.inf
    bins=np.zeros((int(np.ceil(TOP/32)),int(np.ceil(3840/64))),int)
    energy=np.zeros_like(bins,float)
    for c in cs:
        bx=min(bins.shape[1]-1,c['x0']//64); by=min(bins.shape[0]-1,c['y0']//32)
        bins[by,bx]+=c['area']; energy[by,bx]+=c['area']*c['mean']
    return {'centroid_covariance':np.cov(centers.T).tolist(),'nearest_neighbor_px_percentiles':percentiles(d.min(axis=1)),'occupied_64x32_bins':int(np.count_nonzero(bins)),'top_bin_area':int(bins.max()),'top_bin_fraction_of_bin':float(bins.max()/(64*32)),'bins':bins.tolist()}

rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig'))); unique={}
for row in rows: unique.setdefault(row['SHA256'],row['File'])
report={'protocol_sha256':hashlib.sha256((OUT/'protocol.txt').read_bytes()).hexdigest(),'input_identity_count':len(rows),'unique_payload_count':len(unique),'unique_payloads':[],'classifier_rule':{'pass_prior_profile_tests':'neutral>=0.98, Y residual p95<=4,p99<=12, tile step p95<=8,max<=24, chroma residual p99<=8','content_veto':'reject if any 8-connected residual17 abs>=12 component area>=200 OR (bbox width>=24 AND height>=12), OR fixed x0/y0-origin 64x32 bin occupancy>=8%; no peak-only threshold'},'runtime_seconds':None}
t0=time.time()
for digest,name in unique.items():
    y,u,v=decode(ROOT/name); band=y[:TOP,:]; med,resid,summary=profile_summary(band); scale=low_scales(band)
    cs=components(resid,12,8)
    # Occupancy test: per-bin area of thresholded pixels, not component area.
    mask=resid>=12; binmax=0; binxy=None
    for by in range(0,TOP,32):
        for bx in range(0,3840,64):
            n=int(mask[by:min(by+32,TOP),bx:bx+64].sum())
            frac=n/(min(32,TOP-by)*64)
            if frac>binmax:binmax=frac;binxy=[bx,by,n]
    veto=[c for c in cs if c['area']>=200 or (c['w']>=24 and c['h']>=12)]
    rec={'file':name,'sha256':digest,'top_band_shape':list(band.shape),'model17_summary':summary,'spatial':spatial(cs),'candidate_content_veto_components':veto,'max_fixed_bin_occupancy':binmax,'max_fixed_bin_xy_count':binxy,'candidate_morphology_veto':bool(veto or binmax>=.08),'multiscale_separable_medians':scale,'component_coordinate_note':'coordinates relative to top band y=0'}
    report['unique_payloads'].append(rec)
report['runtime_seconds']=round(time.time()-t0,2)
(OUT/'native-multiscale-results.json').write_text(json.dumps(report,indent=2))
print(json.dumps({'protocol_sha256':report['protocol_sha256'],'runtime_seconds':report['runtime_seconds'],'summary':[{'file':r['file'],'components17':r['model17_summary']['components'][2]['count_ge16'],'components17_8conn':r['model17_summary']['components'][5]['count_ge16'],'candidate_morphology_veto':r['candidate_morphology_veto'],'max_bin_occ':r['max_fixed_bin_occupancy'],'spatial':{k:r['spatial'].get(k) for k in ('nearest_neighbor_px_percentiles','occupied_64x32_bins','top_bin_area','top_bin_fraction_of_bin')},'multi':{k:v['absolute_residual_percentiles'] for k,v in r['multiscale_separable_medians'].items()}} for r in report['unique_payloads']]},indent=2))
