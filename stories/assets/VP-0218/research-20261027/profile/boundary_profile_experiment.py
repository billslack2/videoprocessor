from pathlib import Path
import csv, hashlib, json, math
import numpy as np
from scipy import ndimage

ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1')
ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay')
OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-research-20261027\profile')
PARAM={"probe_count":96,"edge_dy_min":20,"edge_chroma_delta_min":16,"edge_support_each_quartile_min":0.70,"full_column_line_tolerance_rows":2,"full_column_line_support_min":0.95,"scan_halves_independent":True,"uncertainty_fringe_rows":8,"neutral_abs_chroma_max":16,"neutral_fraction_min":0.98,"median_background_window":17,"y_residual_p95_max":4,"y_residual_p99_max":12,"component_threshold_y":12,"component_min_area":16,"tile_size":32,"tile_step_p95_max":8,"tile_step_max":24,"chroma_residual_p99_max":8,"synthetic_seed":218}

def decode_v210(path):
    meta=Path(path).with_suffix('.txt').read_text().splitlines()[0].split(); w,h,stride=map(int,meta[:3]); raw=Path(path).read_bytes(); assert len(raw)==stride*h
    words=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4)
    Y=np.empty((h,words.shape[1],6),np.uint16); U=np.empty((h,words.shape[1],3),np.uint16); V=np.empty_like(U)
    for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))): Y[:,:,i]=(words[:,:,n]>>s)&1023
    for i,(n,s) in enumerate(((0,0),(1,10),(2,20))): U[:,:,i]=(words[:,:,n]>>s)&1023
    for i,(n,s) in enumerate(((0,20),(2,0),(3,10))): V[:,:,i]=(words[:,:,n]>>s)&1023
    Y=Y.reshape(h,-1)[:,:w].astype(np.int16); U=np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16); V=np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16)
    return {'y':Y,'u':U,'v':V}

def edge_scan(P,side):
    Y,U,V=(P[k] for k in ('y','u','v')); H,W=Y.shape
    xs=np.linspace(8,W-9,PARAM['probe_count']).astype(int)
    rows=range(0,H//2) if side=='top' else range(H-2,H//2-1,-1)
    for r in rows:
        if side=='top': iy,oy=r+1,r
        else: iy,oy=r,r+1
        dy=Y[iy,xs]-Y[oy,xs]; dc=np.maximum(np.abs(U[iy,xs]-U[oy,xs]),np.abs(V[iy,xs]-V[oy,xs]))
        ok=(dy>=PARAM['edge_dy_min'])&(dc>=PARAM['edge_chroma_delta_min'])
        q=[float(ok[i*24:(i+1)*24].mean()) for i in range(4)]
        if min(q)>=PARAM['edge_support_each_quartile_min']:
            # Validate the nominated line on every pixel column in +/-4 rows, retaining only supported rises.
            lo=max(0,r-4); hi=min(H-2,r+4)
            if side=='top': candidates=range(lo,hi+1)
            else: candidates=range(lo,hi+1)
            best_r=np.full(W,-1,np.int32); best_d=np.full(W,-1,np.int16)
            for rr in candidates:
                yi,yo=(rr+1,rr) if side=='top' else (rr,rr+1)
                d=Y[yi,:]-Y[yo,:]; c=np.maximum(np.abs(U[yi,:]-U[yo,:]),np.abs(V[yi,:]-V[yo,:]))
                d=np.where((d>=PARAM['edge_dy_min'])&(c>=PARAM['edge_chroma_delta_min']),d,-1)
                take=d>best_d;best_d[take]=d[take];best_r[take]=rr
            supported=best_r>=0; err=np.abs(best_r[supported]-r)
            edge_row_q=[]
            for qi in range(4):
                a=qi*W//4;b=(qi+1)*W//4
                edge_row_q.append({"quartile":qi,"sample_support":q[qi],"column_support":float(supported[a:b].mean()),"line_error_p95":float(np.percentile(np.abs(best_r[a:b][best_r[a:b]>=0]-r),95)) if np.any(best_r[a:b]>=0) else None})
            all_line_ok=float(supported.mean())>=PARAM['full_column_line_support_min'] and (float(np.percentile(err,95))<=PARAM['full_column_line_tolerance_rows'] if len(err) else False)
            return {"transition_outside_row":int(r),"inside_row":int(iy),"crop_coordinate_with_fringe":int(iy-PARAM['uncertainty_fringe_rows'] if side=='top' else oy+PARAM['uncertainty_fringe_rows']),"support_by_quartile":q,"sample_dy_median":float(np.median(dy)),"sample_chroma_delta_median":float(np.median(dc)),"full_column_support":float(supported.mean()),"full_column_error_p95":float(np.percentile(err,95)) if len(err) else None,"full_column_line_pass":bool(all_line_ok),"full_column_quartiles":edge_row_q}
    return None

def exterior(P,side,crop):
    Y,U,V=(P[k] for k in ('y','u','v')); H,W=Y.shape
    A=Y[:crop,:] if side=='top' else Y[crop:,:]; AU=U[:crop,:] if side=='top' else U[crop:,:]; AV=V[:crop,:] if side=='top' else V[crop:,:]
    if A.size==0:return {"available":False,"pass":False}
    med=ndimage.median_filter(A,size=PARAM['median_background_window'],mode='reflect')
    ry=np.abs(A.astype(np.float32)-med.astype(np.float32)); ru=np.abs(AU.astype(np.float32)-ndimage.median_filter(AU,size=PARAM['median_background_window'],mode='reflect')); rv=np.abs(AV.astype(np.float32)-ndimage.median_filter(AV,size=PARAM['median_background_window'],mode='reflect'))
    neutral=float(((np.abs(AU-512)<=PARAM['neutral_abs_chroma_max'])&(np.abs(AV-512)<=PARAM['neutral_abs_chroma_max'])).mean())
    mask=ry>=PARAM['component_threshold_y']; lab,n=ndimage.label(mask,structure=np.array([[0,1,0],[1,1,1],[0,1,0]],np.uint8)); counts=np.bincount(lab.ravel()); comps=counts[1:]; largest=int(comps.max()) if len(comps) else 0; component_count=int(np.sum(comps>=PARAM['component_min_area']))
    tile=PARAM['tile_size']; medtiles=[]
    for y0 in range(0,A.shape[0],tile):
        row=[]
        for x0 in range(0,W,tile):row.append(float(np.median(A[y0:min(y0+tile,A.shape[0]),x0:min(x0+tile,W)])))
        medtiles.append(row)
    T=np.asarray(medtiles); steps=[]
    if T.shape[1]>1: steps.extend(np.abs(np.diff(T,axis=1)).ravel().tolist())
    if T.shape[0]>1: steps.extend(np.abs(np.diff(T,axis=0)).ravel().tolist())
    p95step=float(np.percentile(steps,95)) if steps else 0.; maxstep=float(max(steps)) if steps else 0.
    vals={"available":True,"dimensions":[int(A.shape[0]),int(A.shape[1])],"y_percentiles":[float(x) for x in np.percentile(A,[0,50,90,95,99,100])],"neutral_fraction":neutral,"y_highpass_residual_p95_p99_max":[float(x) for x in np.percentile(ry,[95,99,100])],"chroma_highpass_residual_p99_max":[float(np.percentile(ru,99)),float(np.percentile(rv,99))],"connected_y_residual_components_ge16":component_count,"largest_y_residual_component":largest,"tile_median_step_p95_max":[p95step,maxstep],"pass_neutral":neutral>=PARAM['neutral_fraction_min'],"pass_y_residual":float(np.percentile(ry,95))<=PARAM['y_residual_p95_max'] and float(np.percentile(ry,99))<=PARAM['y_residual_p99_max'],"pass_components":component_count==0,"pass_tile_slope":p95step<=PARAM['tile_step_p95_max'] and maxstep<=PARAM['tile_step_max'],"pass_chroma_residual":max(float(np.percentile(ru,99)),float(np.percentile(rv,99)))<=PARAM['chroma_residual_p99_max']}
    vals['pass']=all(vals[k] for k in ('pass_neutral','pass_y_residual','pass_components','pass_tile_slope','pass_chroma_residual'))
    return vals

def analyze(P,label):
    Y=P['y']; H,W=Y.shape; edges={s:edge_scan(P,s) for s in ('top','bottom')}; ext={}
    for s in ('top','bottom'):
        e=edges[s]
        if not e: ext[s]={"available":False,"pass":False};continue
        ext[s]=exterior(P,s,e['crop_coordinate_with_fringe'])
    return {"label":label,"size":[int(W),int(H)],"edges":edges,"exteriors":ext,"edge_pass":all(bool(edges[s] and edges[s]['full_column_line_pass']) for s in ('top','bottom')),"route_pass":all(bool(edges[s] and edges[s]['full_column_line_pass'] and ext[s].get('pass',False)) for s in ('top','bottom'))}

def synthetic(kind):
    H,W=720,1280; yy,xx=np.mgrid[0:H,0:W]; top,bottom=130,590
    y=np.full((H,W),64.,np.float32);u=np.full((H,W),512.,np.float32);v=np.full((H,W),512.,np.float32)
    # Each exterior has its own baseline: top is a spatially varying neutral gray; bottom flat black.
    topbase=64+0.08*xx[:top,:]/W*100+0.12*yy[:top,:]
    y[:top,:]=topbase; y[bottom:,:]=64
    if kind in ('gradient_matte','authored_ceiling_identical_pixels','fine_caption','colored_object','broad_low_contrast','gradient_noise'):
        y[130:590,:]=260;u[130:590,:]=460;v[130:590,:]=760
    if kind=='fine_caption':
        # Two sparse, connected, white glyph-like strokes inside top exterior, away from the edge uncertainty fringe.
        y[78:82,180:420]=820; y[82:102,180:186]=820; y[98:102,180:420]=820
        y[82:102,300:306]=820; y[82:88,300:420]=820; y[94:100,300:420]=820
    elif kind=='colored_object':
        y[55:95,500:760]=300;u[55:95,500:760]=900;v[55:95,500:760]=250
    elif kind=='broad_low_contrast':
        # Smooth broad low contrast neutral object, intentionally tests baseline absorption.
        cx,cy=650,75; rad=((xx[:top,:]-cx)/180)**2+((yy[:top,:]-cy)/55)**2
        y[:top,:]+=8*np.exp(-0.5*rad)
    elif kind=='gradient_noise':
        rng=np.random.default_rng(PARAM['synthetic_seed']); y[:top,:]+=rng.normal(0,4,(top,W))
    y=np.clip(np.rint(y),0,1023).astype(np.int16);u=np.clip(np.rint(u),0,1023).astype(np.int16);v=np.clip(np.rint(v),0,1023).astype(np.int16)
    if kind=='authored_ceiling_identical_pixels':
        # Semantic label differs; pixel planes are exactly the positive gradient case.
        pass
    return {'y':y,'u':u,'v':v}

# Use each unique native payload once and separately record repeated capture identities.
hash_rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')))
unique={}
for row in hash_rows: unique.setdefault(row['SHA256'],row['File'])
actual=[]
for digest,name in unique.items():
    P=decode_v210(ROOT/name); result=analyze(P,'native_'+name)
    result['payload_sha256']=digest; result['first_identity_file']=name; actual.append(result)
synthetics=[]
for kind in ('gradient_matte','authored_ceiling_identical_pixels','fine_caption','colored_object','broad_low_contrast','gradient_noise'):
    synthetics.append(analyze(synthetic(kind),'synthetic_'+kind))
report={"protocol":PARAM,"native_capture":{"payload_count":len(hash_rows),"unique_payload_count":len(unique),"repeats_by_sha256":{digest:sum(r["SHA256"]==digest for r in hash_rows) for digest in unique},"unique_payload_results":actual},"synthetic_results":synthetics,"prior_linear_fit_reference":{"top_transition":300,"bottom_transition":1856,"top_heldout_y_p95":17.4075,"top_heldout_y_max":90.214,"top_large_residual_components":[4241,4011,2386],"bottom_passed":True},"interpretation":{"production_authority":False,"semantic_ceiling_identity_control":True,"do_not_tune_to_captured_ad":True}}
path=OUT/'boundary-profile-results.json';path.write_text(json.dumps(report,indent=2));print(json.dumps({"native_capture":report['native_capture'],"synthetic_results":report['synthetic_results'],"result_path":str(path)},indent=2))



