from pathlib import Path
import numpy as np
from PIL import Image
import json, hashlib
root=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1')
out=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay')
first=root/'frame-000001.bin'
head=list(map(int,first.with_suffix('.txt').read_text().splitlines()[0].split()))
w,h,stride=head[:3]
assert head[4:7]==[3,3,4],head
assert head[10]==1,head
raw=first.read_bytes()
assert len(raw)==stride*h
words=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4)
y=np.empty((h,words.shape[1],6),dtype=np.uint16)
u=np.empty((h,words.shape[1],3),dtype=np.uint16)
v=np.empty_like(u)
for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))): y[:,:,i]=(words[:,:,n]>>s)&1023
for i,(n,s) in enumerate(((0,0),(1,10),(2,20))): u[:,:,i]=(words[:,:,n]>>s)&1023
for i,(n,s) in enumerate(((0,20),(2,0),(3,10))): v[:,:,i]=(words[:,:,n]>>s)&1023
y=y.reshape(h,-1)[:,:w];u=np.repeat(u.reshape(h,-1),2,axis=1)[:,:w];v=np.repeat(v.reshape(h,-1),2,axis=1)[:,:w]
np.savez(out/'first-frame-planes.npz',y=y,u=u,v=v)
# Diagnostic rendering only: Rec.709 limited-range signal RGB, no calibration/tone mapping.
yf=(y.astype(np.float32)-64)/876;uf=(u.astype(np.float32)-512)/896;vf=(v.astype(np.float32)-512)/896
rgb=np.stack([yf+1.5748*vf,yf-0.187324*uf-0.468124*vf,yf+1.8556*uf],axis=2)
Image.fromarray(np.round(np.clip(rgb,0,1)*255).astype('uint8')).save(out/'first-frame-rec709-preview.png')
Image.fromarray(np.round(np.clip(yf,0,1)*255).astype('uint8')).save(out/'first-frame-luma.png')
quant=np.percentile(y,[0,10,50,90,99,100],axis=1).T
np.savetxt(out/'first-frame-row-profile.csv',np.column_stack([np.arange(h),quant,(y<=104).mean(axis=1),(y<=88).mean(axis=1)]),delimiter=',',header='row,min,p10,median,p90,p99,max,fraction_le104,fraction_le88',comments='',fmt='%.6f')
summary={'source_file':str(first),'source_sha256':hashlib.sha256(raw).hexdigest(),'metadata':head,'preview':'Analytical Rec709 limited signal RGB; not a screen capture or calibrated output.','rows':[{ 'row':r,'y_percentiles':quant[r].tolist(),'u_median':float(np.median(u[r])),'v_median':float(np.median(v[r])),'fraction_le104':float((y[r]<=104).mean())} for r in [0,2,16,64,128,256,300,302,304,320,540,1080,1800,1856,1858,1860,1900,2000,2159]]}
(out/'first-frame-profile-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
