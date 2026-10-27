from pathlib import Path
import csv,hashlib,json
import numpy as np
from scipy import ndimage
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1')
ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay')
OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-research-20261027\profile')
THRESHOLD=12; MIN_AREA=16; FILTER=17; TOP_CROP=293

def decode(path):
    meta=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,meta[:3]); raw=Path(path).read_bytes()
    W=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4)
    Y=np.empty((h,W.shape[1],6),np.uint16);U=np.empty((h,W.shape[1],3),np.uint16);V=np.empty_like(U)
    for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(W[:,:,n]>>s)&1023
    for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(W[:,:,n]>>s)&1023
    for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(W[:,:,n]>>s)&1023
    return Y.reshape(h,-1)[:,:w].astype(np.int16),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16)

def synth(kind):
    H,W=720,1280;yy,xx=np.mgrid[0:H,0:W];top,bottom=130,590
    y=np.full((H,W),64.,np.float32);u=np.full_like(y,512);v=np.full_like(y,512)
    y[:top,:]=64+0.08*xx[:top,:]/W*100+0.12*yy[:top,:];y[130:590,:]=260;u[130:590,:]=460;v[130:590,:]=760;y[bottom:,:]=64
    if kind=='caption':
        y[78:82,180:420]=820;y[82:102,180:186]=820;y[98:102,180:420]=820;y[82:102,300:306]=820;y[82:88,300:420]=820;y[94:100,300:420]=820
    elif kind=='noise':
        rng=np.random.default_rng(218);y[:top,:]+=rng.normal(0,4,(top,W))
    elif kind=='broad_low_contrast':
        rad=((xx[:top,:]-650)/180)**2+((yy[:top,:]-75)/55)**2;y[:top,:]+=8*np.exp(-0.5*rad)
    return np.clip(np.rint(y),0,1023).astype(np.int16)

def comps(y, crop, offset=0):
    A=y[:crop,:]; base=ndimage.median_filter(A,size=FILTER,mode='reflect');res=np.abs(A.astype(np.float32)-base.astype(np.float32));mask=res>=THRESHOLD
    lab,n=ndimage.label(mask,structure=np.array([[0,1,0],[1,1,1],[0,1,0]],np.uint8)); out=[]
    for k,sl in enumerate(ndimage.find_objects(lab),1):
        area=int((lab[sl]==k).sum())
        if area<MIN_AREA:continue
        ys,xs=sl;rr=res[sl][lab[sl]==k]
        out.append({"area":area,"bbox":{"x0":int(xs.start),"x1_exclusive":int(xs.stop),"y0":int(ys.start+offset),"y1_exclusive":int(ys.stop+offset)},"width":int(xs.stop-xs.start),"height":int(ys.stop-ys.start),"peak_residual":int(rr.max()),"mean_residual":float(rr.mean()),"p95_residual":float(np.percentile(rr,95))})
    return sorted(out,key=lambda c:(-c['area'],c['bbox']['y0'],c['bbox']['x0']))

rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));unique={}
for r in rows:unique.setdefault(r['SHA256'],r['File'])
allnative=[]
for digest,name in unique.items():
    y,u,v=decode(ROOT/name); cs=comps(y,TOP_CROP)
    allnative.append({"payload_sha256":digest,"file":name,"component_count_ge16":len(cs),"components":cs})
controls=[]
for k,crop in [('caption',122),('noise',122),('broad_low_contrast',122)]:
    cs=comps(synth(k),crop);controls.append({"case":k,"component_count_ge16":len(cs),"components":cs})
first=allnative[0]['components'];areas=[x['area'] for x in first]
summary={"measurement":{"highpass":"abs(Y - 17x17 median(Y))","threshold_y":THRESHOLD,"connectivity":4,"minimum_component_area":MIN_AREA,"native_candidate_top":TOP_CROP,"note":"Descriptive only; frozen prototype verdict and thresholds unchanged."},"native_unique_payloads":allnative,"native_component_area_summary":{"count_first_payload":len(first),"area_min_median_p95_max":[int(min(areas)),float(np.median(areas)),float(np.percentile(areas,95)),int(max(areas))],"peak_residual_min_median_max":[int(min(x['peak_residual'] for x in first)),float(np.median([x['peak_residual'] for x in first])),int(max(x['peak_residual'] for x in first))]},"synthetic_controls":controls,"bottom_edge_coordinate_erratum":"In boundary-profile-results.json the bottom edge_scan field transition_outside_row actually stores r as the inside row; the outside row is r+1. Correct nominal paired coordinates are inside=1856, outside=1857. This affects label wording only; detection, crop coordinate and exterior mask are unchanged."}
out=OUT/'component-shape-analysis.json';out.write_text(json.dumps(summary,indent=2));print(json.dumps({"native_component_area_summary":summary['native_component_area_summary'],"native_components_first":first,"other_native_counts":[(x['file'],x['component_count_ge16']) for x in allnative[1:]],"synthetic":[{"case":x['case'],"count":x['component_count_ge16'],"components":x['components']} for x in controls],"result":str(out)},indent=2))
