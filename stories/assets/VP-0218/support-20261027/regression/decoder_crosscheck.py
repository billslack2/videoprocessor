from pathlib import Path
import csv,hashlib,json
import numpy as np
cases=[
 ('city-lights',Path(r'E:\codex\diagnostics\city-lights-20261013\frame.bin'),Path(r'E:\codex\diagnostics\city-lights-20261013\frame.bin.txt'),'p210-decoded-dv422'),
 ('thai-top-card',Path(r'E:\codex\diagnostics\thai-top-card-20261012\native.bin'),Path(r'E:\codex\diagnostics\thai-top-card-20261012\native.bin.txt'),'v210'),
 ('latin-three-lines',Path(r'E:\codex\diagnostics\latin-three-lines-20261012\native.bin'),Path(r'E:\codex\diagnostics\latin-three-lines-20261012\native.bin.txt'),'v210')]
out=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression')
def decode(path,sidecar,kind):
 f=sidecar.read_text().splitlines()[0].split();w,h,row,crow,fmt,enc,color,top,bottom,script=map(int,f[:10]);raw=path.read_bytes()
 if kind=='v210':
  words=np.frombuffer(raw,dtype='<u4').reshape(h,row//4)[:,:((w+5)//6)*4].reshape(h,-1,4)
  Y=np.empty((h,words.shape[1],6),np.uint16);U=np.empty((h,words.shape[1],3),np.uint16);V=np.empty_like(U)
  for i,(n,s) in enumerate(((0,10),(1,0),(1,20),(2,10),(3,0),(3,20))):Y[:,:,i]=(words[:,:,n]>>s)&1023
  for i,(n,s) in enumerate(((0,0),(1,10),(2,20))):U[:,:,i]=(words[:,:,n]>>s)&1023
  for i,(n,s) in enumerate(((0,20),(2,0),(3,10))):V[:,:,i]=(words[:,:,n]>>s)&1023
  Y=Y.reshape(h,-1)[:,:w].astype(np.uint16);U=np.repeat(U.reshape(h,-1),2,axis=1)[:,:w].astype(np.uint16);V=np.repeat(V.reshape(h,-1),2,axis=1)[:,:w].astype(np.uint16)
 else:
  u16=np.frombuffer(raw,dtype='<u2'); Y=(u16[:h*row//2].reshape(h,row//2)>>6).astype(np.uint16);uv=(u16[h*row//2:].reshape(h,crow//2)>>6).reshape(h,-1,2).astype(np.uint16);U=np.repeat(uv[:,:,0],2,axis=1)[:,:w];V=np.repeat(uv[:,:,1],2,axis=1)[:,:w]
 return (w,h,row,crow,fmt,enc,color,len(f),Y,U,V)
rows=[]
for label,path,meta,kind in cases:
 w,h,row,crow,fmt,enc,color,nfields,Y,U,V=decode(path,meta,kind)
 decoded=np.fromfile(out/(label+'.yuv16'),dtype='<u2');plane=w*h
 assert decoded.size==3*plane
 ok=[np.array_equal(Y.reshape(-1),decoded[:plane]),np.array_equal(U.reshape(-1),decoded[plane:2*plane]),np.array_equal(V.reshape(-1),decoded[2*plane:])]
 if not all(ok):raise RuntimeError(f'{label} decoded comparison mismatch Y/U/V={ok}')
 rows.append({'name':label,'format_path':kind,'input_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'width':w,'height':h,'rowBytes':row,'chromaRowBytes':crow,'format_enum':fmt,'encoding_enum':enc,'colorspace_enum':color,'sidecar_fields':nfields,'range':'Unknown (no sidecar range field)','compared_pixels_per_plane':plane,'Y_U_V_exact_match_to_VP_AnalysisLumaSource_Sample':True})
 print(label,'exact Y/U/V match over',plane,'pixels; format',fmt,'encoding',enc,'range Unknown')
(out/'decoder-crosscheck.json').write_text(json.dumps(rows,indent=2))
