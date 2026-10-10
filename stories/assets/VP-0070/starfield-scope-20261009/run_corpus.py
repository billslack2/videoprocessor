import cv2,numpy as np,json,hashlib,subprocess,re,sys
from pathlib import Path
p=Path(__file__).parent
video=Path(r'C:\Users\bslac\Videos\2026-10-09 21-10-25.mp4')
fixtures=[('beeping_15',900,22,2538,174,1230,True),('beeping_36',2160,22,2538,174,1230,True),('beeping_50',3000,312,2248,314,1126,True),('autopilot_53',3180,312,2248,314,1126,True),('starfield_14',840,22,2538,174,1230,False),('starfield_13_5',810,22,2538,174,1230,False),('starfield_24',1440,22,2538,174,1230,False)]
manifest=[]
c=cv2.VideoCapture(str(video))
for name,index,left,right,top,bottom,positive in fixtures:
 raw=p/f'frozen-{name}.png'
 if not raw.exists():
  c.set(cv2.CAP_PROP_POS_FRAMES,index);ok,im=c.read();assert ok;cv2.imwrite(str(raw),im)
 im=cv2.imread(str(raw))
 for phase in [0,1]:
  bgra=p/f'{name}-phase{phase}.bgra'
  if not bgra.exists():
   sx=3840/(right-left);sy=1608/(bottom-top)
   m=np.array([[sx,0,-sx*left],[0,sy,276-sy*top+phase]],np.float32)
   dst=cv2.warpAffine(im,m,(3840,2160));cv2.imwrite(str(p/f'{name}-phase{phase}.png'),dst)
   cv2.cvtColor(dst,cv2.COLOR_BGR2BGRA).tofile(str(bgra))
  manifest.append(dict(name=name,frame=index,time=index/60,positive=positive,phase=phase,bgra=bgra.name,physical_picture=[left,top,right,bottom],target_picture=[0,276+phase,3840,1884+phase],frozen_sha256=hashlib.sha256(raw.read_bytes()).hexdigest()))
(p/'corpus-manifest.json').write_text(json.dumps(manifest,indent=2))
variants=sys.argv[1:] or ['baseline','candidate']
for variant in variants:
 results=[]
 for fixture in manifest:
  for pad in [0,2]:
   command=[str(p/'bin'/f'IndependentSubtitleReplay-{variant}.exe'),str(p/fixture['bgra']),'3840','2160',str(276+fixture['phase']),str(1884+fixture['phase']),str(pad)]
   result=subprocess.run(command,capture_output=True,text=True,check=True)
   results.append(dict(**fixture,pad=pad,output=result.stdout))
 (p/f'corpus-{variant}.json').write_text(json.dumps(results,indent=2))
 for r in results:
  print(variant,r['name'],r['phase'],r['pad'],re.findall(r'recovered=.*|gated=.*|reason=.*',r['output'])[:3],flush=True)
