from pathlib import Path
import numpy as np
from PIL import Image
base=Path(r'E:\codex\diagnostics\vp-0218-boundary-support-20261027\regression')
for name in ('city-lights','thai-top-card','latin-three-lines'):
 p=base/(name+'.yuv16'); v=np.fromfile(p,dtype='<u2'); plane=3840*2160; y=v[:plane].reshape(2160,3840)
 view=np.clip((y.astype(np.float32)-64)*255/876,0,255).astype(np.uint8)
 Image.fromarray(view).resize((1920,1080),Image.Resampling.LANCZOS).save(base/(name+'-preview.png'))
 Image.fromarray(view[:720,:]).save(base/(name+'-top.png'))
 print(name, 'Y percentile',np.percentile(y,[0,1,5,25,50,75,95,99,100]).tolist())
