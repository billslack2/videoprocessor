import numpy as np
from PIL import Image
p=r'E:\codex\videoprocessor\ar-retry-test-c4f2b9be\logs\analysis-20261118-130408-46504-1\frame-000001.bin'
a=np.fromfile(p,dtype='<u2',count=3840*2160).reshape(2160,3840)>>6
for y in [0,1,16,64,67,68,69,100,275,276,1883,1884,2091,2092,2159]:
 print(y, 'laneY',a[y,np.arange(1,10)*384].tolist(),'minmax',int(a[y].min()),int(a[y].max()))
Image.fromarray(np.uint8(np.clip(a/1023*255,0,255)))[::] if False else None
Image.fromarray(np.uint8(np.clip(a[::4,::4]/1023*255,0,255))).save('incident-first-fullrange.png')
