import cv2,csv,subprocess,json
from pathlib import Path
import argparse
parser=argparse.ArgumentParser(description="Replay unmodified recorded frames through near-bar detection")
parser.add_argument('--recording',required=True,type=Path)
parser.add_argument('--probe',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
args=parser.parse_args()
manifest=json.loads(Path(__file__).with_name('subtitle_near_bar_recording_manifest.json').read_text())
video=str(args.recording);out=args.output;out.mkdir(parents=True,exist_ok=True)
summary=[]
for sample in manifest["samples"]:
 start=sample["seconds"]
 for distance in [0,20]:
  cap=cv2.VideoCapture(video);cap.set(cv2.CAP_PROP_POS_MSEC,start*1000)
  path=out/f'{start}-{distance}.csv'
  with path.open('wb') as output:
   proc=subprocess.Popen([str(args.probe),str(manifest['width']),str(manifest['height']),'5','--near-bar-px',str(distance)],stdin=subprocess.PIPE,stdout=output,stderr=subprocess.PIPE)
   for i in range(manifest["frames_per_sample"]):
    ok,frame=cap.read()
    if not ok:break
    assert frame.shape[:2]==(manifest["height"],manifest["width"]),"Recording dimensions differ"
    proc.stdin.write(cv2.cvtColor(frame,cv2.COLOR_BGR2BGRA).tobytes())
   proc.stdin.close();err=proc.stderr.read();assert proc.wait()==0,err
  cap.release()
  rows=list(csv.DictReader(path.open()))
  summary.append(dict(start=start,distance=distance,frames=len(rows),detected=sum(int(x['measured']) for x in rows),displayed=sum(int(x['displayed']) for x in rows),valid=sum(int(x['cut_valid']) for x in rows),bounds=[{k:rows[-1][k] for k in ['picture_top','picture_bottom','measured_top','measured_bottom','measured_lines','source_panel_top','source_panel_bottom']}] if rows else []))
  print(summary[-1],flush=True)
  if distance==20:
   assert len(rows)==manifest['frames_per_sample']
   assert all(int(row['measured']) and int(row['displayed']) and int(row['cut_valid']) and int(row['measured_lines'])==sample['lines'] for row in rows), summary[-1]
(out/'summary.json').write_text(json.dumps(summary,indent=2))
