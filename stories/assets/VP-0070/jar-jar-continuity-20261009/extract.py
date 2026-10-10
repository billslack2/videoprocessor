import cv2,numpy as np,subprocess,json
from pathlib import Path
p=Path(r'C:\Users\bslac\Documents\ChatGPT\VP Low Priority\jarjar-sequence-replay')
v=cv2.VideoCapture(r'C:\Users\bslac\Videos\2026-10-09 20-04-02.mp4')
with open(p/'sequence.csv','w') as out:
 proc=subprocess.Popen([str(p/'bin/SequenceReplay.exe')],stdin=subprocess.PIPE,stdout=out)
 meta=[]
 for i in range(109):
  t=14.5+i/24
  v.set(cv2.CAP_PROP_POS_MSEC,t*1000);ok,f=v.read()
  if not ok:break
  crop=f[196:1320,544:2544]
  l=cv2.cvtColor(crop,cv2.COLOR_BGR2GRAY);xs=np.r_[20:450,1550:1980];q=(l[:,xs]>12).mean(1);yy=np.where(q>.20)[0];top=int(yy[0]);bottom=int(yy[-1]+1)
  sy=1608/(bottom-top)
  m=np.float32([[3840/2000,0,0],[0,sy,276-top*sy]])
  source=cv2.warpAffine(crop,m,(3840,2160),flags=cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT)
  source[:276]=0;source[1884:]=cv2.warpAffine(crop,m,(3840,2160),flags=cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT)[1884:]
  if i in [0,30,60,90]:cv2.imwrite(str(p/f'frame{i:03}.png'),source)
  proc.stdin.write(cv2.cvtColor(source,cv2.COLOR_BGR2BGRA).tobytes())
  meta.append({'sequence':i+1,'time':t,'top':top,'bottom':bottom})
 proc.stdin.close();code=proc.wait();print('exit',code,'frames',len(meta))
(p/'mapping.json').write_text(json.dumps(meta,indent=2))
