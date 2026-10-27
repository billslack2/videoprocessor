from pathlib import Path
import csv,hashlib,json
import numpy as np
from scipy import ndimage
ROOT=Path(r'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1')
ARCH=Path(r'E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay')
OUT=Path(r'E:\codex\diagnostics\vp-0218-boundary-research-20261027\profile\normalized-ablation')
TOP=293;BOTTOM=1865;H_EXPECT=2160;W_EXPECT=3840

def decode(path):
 m=Path(path).with_suffix('.txt').read_text().splitlines()[0].split();w,h,stride=map(int,m[:3]);raw=Path(path).read_bytes();assert (w,h)==(W_EXPECT,H_EXPECT) and len(raw)==stride*h
 words=np.frombuffer(raw,dtype='<u4').reshape(h,stride//4)[:,:((w+5)//6)*4].reshape(h,-1,4)
 Y=np.empty((h,words.shape[1],6),np.uint16);U=np.empty((h,words.shape[1],3),np.uint16);V=np.empty_like(U)
 for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(words[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(words[:,:,n]>>s)&1023
 for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(words[:,:,n]>>s)&1023
 return Y.reshape(h,-1)[:,:w].astype(np.int16),np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16),np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(np.int16)

hash_rows=list(csv.DictReader((ARCH/'capture-frame-hashes.csv').open(encoding='utf-8-sig')));unique={}
for r in hash_rows:unique.setdefault(r['SHA256'],r['File'])
records=[]
for digest,name in unique.items():
 Y,U,V=decode(ROOT/name); original=Y.copy();clipped_low=clipped_high=0;resparts=[]
 for a,b,side in ((0,TOP,'top'),(BOTTOM,H_EXPECT,'bottom')):
  src=original[a:b,:];base=ndimage.median_filter(src,size=17,mode='reflect').astype(np.int16);res=src-base
  cf64=64+res.astype(np.int32);clipped_low+=int((cf64<0).sum());clipped_high+=int((cf64>1023).sum())
  Y[a:b,:]=np.clip(cf64,0,1023).astype(np.int16)
  resparts.append({'side':side,'shape':[b-a,W_EXPECT],'residual_percentiles':[float(x) for x in np.percentile(res,[0,1,5,50,95,99,100])],'normalized_y_percentiles':[float(x) for x in np.percentile(Y[a:b,:],[0,1,5,50,95,99,100])]})
 uv=np.empty((H_EXPECT,W_EXPECT//2,2),np.uint16);uv[:,:,0]=U[:,::2].astype(np.uint16);uv[:,:,1]=V[:,::2].astype(np.uint16)
 raw=(Y.astype(np.uint16)<<6).astype('<u2').tobytes()+(uv<<6).astype('<u2').tobytes()
 decoded=np.frombuffer(raw,dtype='<u2');y2=(decoded[:H_EXPECT*W_EXPECT].reshape(H_EXPECT,W_EXPECT)>>6).astype(np.int16);uv2=(decoded[H_EXPECT*W_EXPECT:].reshape(H_EXPECT,W_EXPECT//2,2)>>6).astype(np.int16);u2=np.repeat(uv2[:,:,0],2,axis=1);v2=np.repeat(uv2[:,:,1],2,axis=1)
 assert np.array_equal(y2,Y) and np.array_equal(u2,U) and np.array_equal(v2,V)
 fn='native_'+name.replace('.bin','.p210');ep=fn.replace('.p210','.expected');(OUT/fn).write_bytes(raw)
 expected=np.concatenate((Y.reshape(-1),U.reshape(-1),V.reshape(-1))).astype('<i2').tobytes();(OUT/ep).write_bytes(expected)
 records.append({'source_file':name,'source_sha256':digest,'counterfactual_file':fn,'counterfactual_sha256':hashlib.sha256(raw).hexdigest(),'expected_file':ep,'expected_sha256':hashlib.sha256(expected).hexdigest(),'uv_sha256_before_after_identical':hashlib.sha256(U.astype('<i2').tobytes()+V.astype('<i2').tobytes()).hexdigest(),'clipped_low_count':clipped_low,'clipped_high_count':clipped_high,'profiles':resparts,'python_p210_roundtrip':'all Y/U/V exact','changed_y_pixels':int((Y!=original).sum()),'changed_uv_samples':int((U!=decode(ROOT/name)[1]).sum()+(V!=decode(ROOT/name)[2]).sum())})
manifest={'source_capture_payload_count':len(hash_rows),'unique_payload_count':len(unique),'candidate_bounds':{'top':TOP,'bottom_exclusive':BOTTOM},'operation':'Exterior-only 17x17 median residual +64, clipped [0,1023]; interior/8-row fringe/chroma preserved. Copies are counterfactual, not crop authority.','dimensions':[W_EXPECT,H_EXPECT],'p210':'full Y plane then interleaved UV; high 10 bits in little-endian lanes','records':records}
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2));print(json.dumps(manifest,indent=2))
