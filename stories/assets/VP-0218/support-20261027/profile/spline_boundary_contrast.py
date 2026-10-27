from pathlib import Path
import csv,json,hashlib
import numpy as np
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1');ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay');OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\profile')
def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();P=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4);Y=np.empty((h,P.shape[1],6),np.uint16)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(P[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(float)
rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));u={}
for r in rows:u.setdefault(r['SHA256'],r['File'])
out=[]
for sha,name in u.items():
 y=decode(ROOT/name);h,w=y.shape;vals=[]
 for a,b,side in ((0,293,'top'),(1865,2160,'bottom')):
  depth=b-a;yy=((2*np.arange(6)+1)*depth)//12;yy=yy if side=='top' else h-1-yy;xx=((2*np.arange(48)+1)*w)//96;sy=y[np.ix_(yy,xx)];boundary=b if side=='top' else a;insideY=boundary+2 if side=='top' else boundary-3;contrast=float(y[insideY,xx].mean()-sy.mean());vals.append({'side':side,'sampled_band_mean':float(sy.mean()),'inside_guard_row':int(insideY),'inside_guard_sampled_mean':float(y[insideY,xx].mean()),'raw_boundary_contrast_at_guard':contrast})
 out.append({'file':name,'sha256':sha,'edges':vals})
report={'reference_formula_source_sha256':hashlib.sha256((OUT.parent/'reference'/'compare_reference.py').read_bytes()).hexdigest(),'height_candidates':[293,1865],'records':out}
(OUT/'spline-boundary-contrast.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
