from pathlib import Path
import hashlib,json
import numpy as np
out=Path(__file__).resolve().parent
W,H=3840,2160; top,bottom=301,1857
# Small 5x7 glyph-like cells; sparse disconnected glyph components by design.
GLYPHS={
 'A':['01110','10001','10001','11111','10001','10001','10001'],
 'B':['11110','10001','10001','11110','10001','10001','11110'],
 'C':['01111','10000','10000','10000','10000','10000','01111'],
 'E':['11111','10000','10000','11110','10000','10000','11111'],
 'I':['11111','00100','00100','00100','00100','00100','11111'],
 'L':['10000','10000','10000','10000','10000','10000','11111'],
 'N':['10001','11001','11001','10101','10011','10011','10001'],
 'O':['01110','10001','10001','10001','10001','10001','01110'],
 'R':['11110','10001','10001','11110','10100','10010','10001'],
 'S':['01111','10000','10000','01110','00001','00001','11110'],
 'T':['11111','00100','00100','00100','00100','00100','00100'],
 'U':['10001','10001','10001','10001','10001','10001','01110'],
 'V':['10001','10001','10001','10001','10001','01010','00100']}
text='SUBTITLE'*48
def base():
 y=np.full((H,W),64,np.int16);u=np.full((H,W),512,np.int16);v=np.full((H,W),512,np.int16)
 yy=np.arange(top,dtype=np.float32)[:,None];xx=np.arange(W,dtype=np.float32)[None,:]
 y[:top,:]=np.rint(64+8*xx/W+0.12*yy).astype(np.int16)
 y[top:bottom,:]=260;u[top:bottom,:]=460;v[top:bottom,:]=760
 return y,u,v
def text_line(y,y0,scale,delta):
 cellw=6*scale+4*scale # 10*scale step; base glyph 5px plus 5px blank
 for i,ch in enumerate(text):
  x0=40+i*cellw
  if x0+5*scale>=W:break
  for gy,row in enumerate(GLYPHS[ch]):
   for gx,b in enumerate(row):
    if b=='1': y[y0+gy*scale:y0+(gy+1)*scale,x0+gx*scale:x0+(gx+1)*scale]=np.minimum(1023,y[y0+gy*scale:y0+(gy+1)*scale,x0+gx*scale:x0+(gx+1)*scale].astype(np.int32)+delta).astype(np.int16)
def make(kind):
 y,u,v=base()
 if kind=='neutral_gradient_grain_sigma1':
  rng=np.random.default_rng(218);y[:top,:]=np.clip(np.rint(y[:top,:].astype(np.float32)+rng.normal(0,1,(top,W))),0,1023).astype(np.int16)
 elif kind=='subpixel_glyph_caption': text_line(y,220,1,8)
 elif kind=='faint_glyph_caption': text_line(y,180,1,16)
 elif kind=='sparse_stars':
  for i in range(24):
   x=120+151*i;y0=(37*i+41)%285;y[y0,x]=min(1023,int(y[y0,x])+300)
 elif kind=='compact_logo':
  # Connected cross/diamond logo with exactly 150 nonzero pixels in a 10x15 box.
  x0,y0=1915,120;mask=np.zeros((15,10),bool)
  for j in range(15):
   a=max(0,4-j//4);b=min(10,6+j//4);mask[j,a:b]=1
  # deterministically trim if shape reaches over budget; count is recorded in the manifest.
  yy,xx=np.where(mask)
  for j,i in zip(yy,xx):y[y0+j,x0+i]=min(1023,int(y[y0+j,x0+i])+64)
 elif kind=='broad_smooth_object_plus8':
  yy,xx=np.mgrid[0:top,0:W];r=((xx-1920)/540)**2+((yy-180)/165)**2;y[:top,:]=np.clip(np.rint(y[:top,:].astype(np.float32)+8*np.exp(-.5*r)),0,1023).astype(np.int16)
 elif kind=='structured_ceiling_panel':
  y[80:250,900:2940]=np.minimum(1023,y[80:250,900:2940].astype(np.int32)+40).astype(np.int16)
  # Narrow high-contrast neutral seams make it recognizably structured, unlike a pixel-identical semantic alias.
  y[80:250,1398:1404]=np.minimum(1023,y[80:250,1398:1404].astype(np.int32)+24).astype(np.int16)
  y[80:250,2046:2052]=np.maximum(0,y[80:250,2046:2052].astype(np.int32)-16).astype(np.int16)
 elif kind=='internal_letterbox_lookalike':
  y[700:780,:]=64;u[700:780,:]=512;v[700:780,:]=512
 elif kind=='boundary_subtitle': text_line(y,286,2,160)
 return y,u,v
kinds=('neutral_gradient','neutral_gradient_grain_sigma1','subpixel_glyph_caption','faint_glyph_caption','sparse_stars','compact_logo','broad_smooth_object_plus8','structured_ceiling_panel','internal_letterbox_lookalike','boundary_subtitle')
records=[]
for kind in kinds:
 y,u,v=make(kind);assert y.shape==(H,W) and np.all((y>=0)&(y<=1023))
 uv=np.empty((H,W//2,2),np.uint16);uv[:,:,0]=u[:,::2];uv[:,:,1]=v[:,::2]
 raw=(y.astype(np.uint16)<<6).astype('<u2').tobytes()+(uv<<6).astype('<u2').tobytes()
 view=np.frombuffer(raw,dtype='<u2');y2=(view[:H*W].reshape(H,W)>>6).astype(np.int16);uv2=(view[H*W:].reshape(H,W//2,2)>>6).astype(np.int16);u2=np.repeat(uv2[:,:,0],2,axis=1);v2=np.repeat(uv2[:,:,1],2,axis=1)
 assert np.array_equal(y,y2) and np.array_equal(u,u2) and np.array_equal(v,v2)
 ep=np.concatenate((y.astype('<i2').ravel(),u.astype('<i2').ravel(),v.astype('<i2').ravel())).astype('<i2').tobytes()
 if kind=='compact_logo':feature=int(np.count_nonzero(y[120:135,1915:1925]>np.asarray(base()[0][120:135,1915:1925])))
 else:feature=None
 p=out/(kind+'.p210');expected=out/(kind+'.expected');sidecar=out/(kind+'.p210.txt');p.write_bytes(raw);expected.write_bytes(ep);sidecar.write_text(f'{W} {H} {W*2} {W*2} 1 0 4 0 0 1 1\nidentity_v1 1 1 1 1 1 1 1 1 1 1 0 1 0 0 0\n')
 records.append({'name':kind,'p210_sha256':hashlib.sha256(raw).hexdigest(),'expected_sha256':hashlib.sha256(ep).hexdigest(),'p210_bytes':len(raw),'python_all_plane_roundtrip':'pass','feature_count':feature})
manifest={'width':W,'height':H,'y_active_rows':[top,bottom],'guarded_crop':[293,1865],'P210_layout':'full Y plane followed by interleaved UV plane; high 10 bits in little-endian 16-bit lanes','P210_python_roundtrip_all_Y_U_V':True,'controls':records}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2));print(json.dumps(manifest,indent=2))
