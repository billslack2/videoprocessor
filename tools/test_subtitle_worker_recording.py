import argparse,cv2,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--recording',required=True,type=Path)
p.add_argument('--probe',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);results=[]
# Fixed-caption stress cases from the unprocessed section of this recording.
# Upscaling stresses a 4K capture raster; this is not end-to-end GPU playback.
for seconds,expected in [(8,0),(14,90),(20,90)]:
 c=cv2.VideoCapture(str(a.recording));c.set(cv2.CAP_PROP_POS_MSEC,seconds*1000)
 ok,frame=c.read();c.release();assert ok
 raw=a.output/f'frame-{seconds}-4k.bgra'
 cv2.cvtColor(cv2.resize(frame,(3840,2160)),cv2.COLOR_BGR2BGRA).tofile(str(raw))
 for fps in [24,60]:
  run=subprocess.run([str(a.probe),str(raw), '3840','2160',str(fps),'90'],capture_output=True,text=True)
  assert run.returncode==0,(run.stdout,run.stderr)
  result=json.loads(run.stdout);result['recording_seconds']=seconds
  assert result['ready']==90 and result['misses']==0 and result['detected']==expected,result
  assert result['max_poll_ms']<5,result
  results.append(result);print(result,flush=True)
(a.output/'summary.json').write_text(json.dumps(results,indent=2))
