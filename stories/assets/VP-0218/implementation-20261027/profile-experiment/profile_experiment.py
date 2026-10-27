import numpy as np, json, os
from pathlib import Path
p=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay\first-frame-planes.npz')
out=Path(r'E:\codex\diagnostics\vp-0218-implementation-20261027\profile-experiment')
z=np.load(p); planes={k:z[k].astype(np.float64) for k in ('y','u','v')}; H,W=planes['y'].shape
# Frozen experiment limits, set before evaluating result.
GRID=16; XSTEP=8; EDGE_DELTA_Y=20; EDGE_DELTA_CHROMA=16; EDGE_SUPPORT=0.70
FIT_MAX_ABS_Y=8.0; FIT_P95_ABS_Y=4.0; FIT_MAX_ABS_UV=8.0; NEUTRAL_MAX=16.0
COMPONENT_MIN=8; RESIDUAL_COMPONENT_THRESHOLD=12.0; FRINGE_DEPTH=8

def localize(which):
    # Independent transition scan. Compare row medians over 96 spread columns;
    # require a positive luma step and chroma corroboration in >=70% of samples.
    xs=np.linspace(8,W-9,96).astype(int)
    pairs=range(0,H-1) if which=='top' else range(H-2,-1,-1)
    for r in pairs:
        if which=='top' and r>H//2: break
        if which=='bottom' and r<H//2: break
        a=r if which=='top' else r+1; b=a+1
        a,b=(r,r+1) if which=='top' else (r+1,r)
        du=planes['u'][b,xs]-planes['u'][a,xs]; dv=planes['v'][b,xs]-planes['v'][a,xs]
        dy=planes['y'][b,xs]-planes['y'][a,xs]
        support=(dy>=EDGE_DELTA_Y)&((np.abs(du)>=EDGE_DELTA_CHROMA)|(np.abs(dv)>=EDGE_DELTA_CHROMA))
        qs=[support[i*24:(i+1)*24].mean() for i in range(4)]
        if min(qs)>=EDGE_SUPPORT:
            return {'transition_row':int(r),'inside_row':int(b),'support_by_quartile':[float(x) for x in qs],
              'median_dy':float(np.median(dy)),'median_du':float(np.median(du)),'median_dv':float(np.median(dv))}
    return None

def profile(which,boundary):
    # Crop excludes from raster edge to transition; do not infer opposite boundary.
    rows=np.arange(0,boundary) if which=='top' else np.arange(boundary+1,H)
    if len(rows)<=FRINGE_DEPTH+32: return {'error':'band too small'}
    # Hold out every fourth exterior depth and every other column in each grid.
    # Exclude uncertain 8 rows at the transition from fitting, but still measure them.
    fit_rows=rows[:-FRINGE_DEPTH][::4]
    hold_rows=rows[1:]
    xs=np.arange(0,W,XSTEP)
    grid=np.minimum(GRID-1,(xs*GRID)//W)
    result={'band_pixels':int(len(rows)),'fit_depth_count':len(fit_rows),'hold_depth_count':len(hold_rows),'grid':GRID}
    preds={}; residuals={}
    for name,A in planes.items():
      vals=A[np.ix_(fit_rows,xs)]
      pred=np.zeros((len(hold_rows),len(xs)))
      for g in range(GRID):
        ix=np.where(grid==g)[0]
        # Fit constant + linear depth per coarse horizontal cell.
        coef=np.polyfit(fit_rows.astype(float), vals[:,ix].mean(axis=1),1)
        pred[:,ix]=np.polyval(coef,hold_rows)[:,None]
      obs=A[np.ix_(hold_rows,xs)]
      residuals[name]=obs-pred; preds[name]=pred
      ar=np.abs(residuals[name])
      result[name+'_resid']={'p50':float(np.percentile(ar,50)),'p95':float(np.percentile(ar,95)),'p99':float(np.percentile(ar,99)),'max':float(ar.max())}
    # Chroma neutrality against nominal 512, and maximum heldout residual.
    yres=np.abs(residuals['y']); ures=np.abs(residuals['u']); vres=np.abs(residuals['v'])
    raw_u=planes['u'][np.ix_(rows,xs)]; raw_v=planes['v'][np.ix_(rows,xs)]
    result['neutral_fraction']=float(((np.abs(raw_u-512)<=NEUTRAL_MAX)&(np.abs(raw_v-512)<=NEUTRAL_MAX)).mean())
    # 4-neighbor components on heldout positions (row, x) whose luma residual is high.
    mask=(yres>=RESIDUAL_COMPONENT_THRESHOLD)
    seen=np.zeros_like(mask,bool); comps=[]
    for iy,ix in zip(*np.where(mask)):
      if seen[iy,ix]: continue
      stack=[(iy,ix)]; seen[iy,ix]=1; n=0; minx=maxx=ix; miny=maxy=iy
      while stack:
        yy,xx=stack.pop(); n+=1; minx=min(minx,xx); maxx=max(maxx,xx); miny=min(miny,yy); maxy=max(maxy,yy)
        for ny,nx in ((yy-1,xx),(yy+1,xx),(yy,xx-1),(yy,xx+1)):
          if 0<=ny<mask.shape[0] and 0<=nx<mask.shape[1] and mask[ny,nx] and not seen[ny,nx]: seen[ny,nx]=1; stack.append((ny,nx))
      comps.append({'cells':n,'x_span':int(maxx-minx+1),'depth_span':int(maxy-miny+1)})
    comps.sort(key=lambda q:q['cells'],reverse=True)
    result['large_residual_components']=[c for c in comps if c['cells']>=COMPONENT_MIN][:20]
    result['max_abs_chroma_resid']=float(max(ures.max(),vres.max()))
    result['passed_frozen_limits']=bool(result['y_resid']['max']<=FIT_MAX_ABS_Y and result['y_resid']['p95']<=FIT_P95_ABS_Y and result['max_abs_chroma_resid']<=FIT_MAX_ABS_UV and result['neutral_fraction']>=0.95 and not result['large_residual_components'])
    return result

summary={'input':str(p),'shape':[H,W],'frozen_limits':{'grid':GRID,'x_step':XSTEP,'edge_delta_y':EDGE_DELTA_Y,'edge_delta_chroma':EDGE_DELTA_CHROMA,'edge_support_each_quartile':EDGE_SUPPORT,'fit_max_abs_y':FIT_MAX_ABS_Y,'fit_p95_abs_y':FIT_P95_ABS_Y,'fit_max_abs_uv':FIT_MAX_ABS_UV,'neutral_max_from_512':NEUTRAL_MAX,'component_threshold_y':RESIDUAL_COMPONENT_THRESHOLD,'component_min_cells':COMPONENT_MIN,'fringe_depth':FRINGE_DEPTH},'nomination':{},'profiles':{}}
for side in ('top','bottom'):
  loc=localize(side); summary['nomination'][side]=loc
  if loc:
    candidate_boundary=loc['transition_row'] + (FRINGE_DEPTH if side=='bottom' else -FRINGE_DEPTH)
    summary['profiles'][side]=profile(side,candidate_boundary)
    summary['profiles'][side]['candidate_boundary_with_retained_fringe']=candidate_boundary
# Synthetic grid with fixed 16-cell smooth slope baseline, plus specified hard negatives.
# Match retained model family and fixed profile thresholds.
def synth_case(kind):
  yy,xx=np.mgrid[0:160,0:640]; base=120+0.12*xx+0.04*yy
  img=base.copy(); u=np.full_like(img,512.); v=np.full_like(img,512.)
  if kind=='ceiling': img += 0.25*xx + 0.08*yy # smooth authored ceiling gradient
  elif kind=='floor': img += 0.18*xx - 0.07*yy
  elif kind=='authored_ceiling_matched_profile': img += 0.04*xx + 0.02*yy
  elif kind=='connected_object': img[45:75,220:300]+=35
  # create sharp boundary at y=160 into vivid picture
  full=np.vstack([img,np.full((30,640),500.)]); fu=np.vstack([u,np.full((30,640),600.)]); fv=np.vstack([v,np.full((30,640),400.)])
  # analyze only exterior with identical fit design
  rows=np.arange(0,160); xs=np.arange(0,640,XSTEP); grid=np.minimum(GRID-1,(xs*GRID)//640); fit_rows=rows[:-FRINGE_DEPTH][::4]; hold_rows=rows[1:]
  resid={}
  for name,A in [('y',full),('u',fu),('v',fv)]:
   vals=A[np.ix_(fit_rows,xs)]; pred=np.zeros((len(hold_rows),len(xs)))
   for g in range(GRID):
    ix=np.where(grid==g)[0]; co=np.polyfit(fit_rows.astype(float),vals[:,ix].mean(axis=1),1); pred[:,ix]=np.polyval(co,hold_rows)[:,None]
   resid[name]=np.abs(A[np.ix_(hold_rows,xs)]-pred)
  y=resid['y']; m=y>=RESIDUAL_COMPONENT_THRESHOLD; seen=np.zeros_like(m,bool); biggest=0
  for iy,ix in zip(*np.where(m)):
   if seen[iy,ix]:continue
   todo=[(iy,ix)];seen[iy,ix]=1;n=0
   while todo:
    a,b=todo.pop();n+=1
    for c,d in ((a-1,b),(a+1,b),(a,b-1),(a,b+1)):
     if 0<=c<m.shape[0] and 0<=d<m.shape[1] and m[c,d] and not seen[c,d]:seen[c,d]=1;todo.append((c,d))
   biggest=max(biggest,n)
  neutral=float(((np.abs(fu[np.ix_(rows,xs)]-512)<=NEUTRAL_MAX)&(np.abs(fv[np.ix_(rows,xs)]-512)<=NEUTRAL_MAX)).mean())
  return {'y_resid_p95':float(np.percentile(y,95)),'y_resid_max':float(y.max()),'uv_resid_max':float(max(resid['u'].max(),resid['v'].max())),'neutral_fraction':neutral,'largest_component_cells':biggest,'pass_limits':bool(y.max()<=FIT_MAX_ABS_Y and np.percentile(y,95)<=FIT_P95_ABS_Y and max(resid['u'].max(),resid['v'].max())<=FIT_MAX_ABS_UV and neutral>=0.95 and biggest<COMPONENT_MIN)}
summary['synthetic']= {k:synth_case(k) for k in ('gradient_matte','ceiling','floor','connected_object','authored_ceiling_matched_profile')}
(out/'experiment.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
