"""Replay labelled synthetic captions over real movie frames through C++.
Requires numpy/opencv and the built SubtitleBoxProbe.exe. No OCR/model.
"""
import argparse, csv, json, subprocess
from pathlib import Path
import cv2
import numpy as np
ap=argparse.ArgumentParser()
ap.add_argument('--video',required=True);ap.add_argument('--probe',required=True)
ap.add_argument('--output',required=True);ap.add_argument('--scale',type=int,default=1);args=ap.parse_args()
if args.scale not in [1,2]:raise ValueError('scale must be 1 or 2')
out=Path(args.output);out.mkdir(parents=True,exist_ok=True)
cap=cv2.VideoCapture(args.video)
backgrounds=[]
for seconds in [3,7,14,23,34,43]:
 cap.set(cv2.CAP_PROP_POS_MSEC,seconds*1000);ok,f=cap.read()
 if not ok: raise RuntimeError('Cannot read reference frame')
 backgrounds.append(cv2.resize(f,(1920,1080)))
cap.release()
frames=[];labels=[]
def text(f,mask,value,xy):
 cv2.putText(f,value,xy,cv2.FONT_HERSHEY_SIMPLEX,1.1,(0,0,0),6,cv2.LINE_AA)
 cv2.putText(f,value,xy,cv2.FONT_HERSHEY_SIMPLEX,1.1,(240,240,240),2,cv2.LINE_AA)
 cv2.putText(mask,value,xy,cv2.FONT_HERSHEY_SIMPLEX,1.1,255,2,cv2.LINE_AA)
for mode in ['blank','topbar','topcross','bottomcross','bottomtwo','bar','short','picture','menu']:
 for i,bg in enumerate(backgrounds):
  f=bg.copy();f[:120]=0;f[960:]=0;mask=np.zeros((1080,1920),np.uint8)
  if mode=='topbar':text(f,mask,'TOP BAR SUBTITLE',(740,70))
  if mode=='topcross':text(f,mask,'TOP BOUNDARY SUBTITLE',(700,138))
  if mode=='bottomcross':text(f,mask,'BOTTOM BOUNDARY SUBTITLE',(640,978))
  if mode=='bottomtwo':
   text(f,mask,'THE UPPER LINE IS WIDER AND IN THE PICTURE',(520,937))
   text(f,mask,'BOTTOM LINE',(780,978))
  if mode=='bar':text(f,mask,'SUBTITLE ENTIRELY IN THE BAR',(630,1030))
  if mode=='short':text(f,mask,'No.',(915,978))
  if mode=='picture':text(f,mask,'PICTURE ONLY',(750,900))
  if mode=='menu':text(f,mask,'OPTIONS       SETTINGS       EXIT',(550,946))
  positive=mode not in ['blank','picture','menu']
  # Test onset directly after a clean frame; then repeated cue continuity.
  blank=bg.copy();blank[:120]=0;blank[960:]=0
  for _ in range(3):frames.append(blank);labels.append({'mode':'gap','positive':False,'glyph_box':None})
  yy,xx=np.nonzero(mask)
  box=[int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)] if len(xx) else None
  for repeat in range(3):frames.append(f);labels.append({'mode':mode,'background':i,'repeat':repeat,'positive':positive,'glyph_box':box})
# Same cue across scene changes must retain a tight, constant rectangle.
for repeat in range(3):
 for i,bg in enumerate(backgrounds):
  f=bg.copy();f[:120]=0;f[960:]=0;mask=np.zeros((1080,1920),np.uint8)
  text(f,mask,'THE UPPER LINE IS WIDER AND IN THE PICTURE',(520,937));text(f,mask,'BOTTOM LINE',(780,978))
  yy,xx=np.nonzero(mask);box=[int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)]
  frames.append(f);labels.append(dict(mode='continuous_two_line',background=i,repeat=repeat,positive=True,glyph_box=box))
with (out/'frames.csv').open('w',newline='') as result:
 proc=subprocess.Popen([args.probe,str(1920*args.scale),str(1080*args.scale),str(120*args.scale),str(960*args.scale)],stdin=subprocess.PIPE,stdout=result)
 for f in frames:
  if args.scale!=1:f=cv2.resize(f,None,fx=args.scale,fy=args.scale)
  ycc=cv2.cvtColor(f,cv2.COLOR_BGR2YCrCb)
  y=ycc[:,:,0].astype(np.uint16)
  uv=cv2.resize(ycc[:,:,1:],(960*args.scale,540*args.scale),interpolation=cv2.INTER_AREA)[:,:,::-1].copy().astype(np.uint16)
  # Full-range ten-bit luma and 4:2:0 interleaved CbCr in P010 storage.
  proc.stdin.write(((y*4)<<6).tobytes());proc.stdin.write(((uv*4)<<6).tobytes())
 proc.stdin.close()
 if proc.wait()!=0:raise RuntimeError('probe failed')
rows=list(csv.DictReader((out/'frames.csv').open()))
summary={};cost=[];examples=[]
for n,(row,label) in enumerate(zip(rows,labels)):
 cost.append(float(row['cost_ms']))
 if label['mode']=='gap':continue
 mode=label['mode'];m=summary.setdefault(mode,dict(frames=0,detected=0,complete=0,onsets=0,complete_onsets=0,area_ratios=[]))
 m['frames']+=1;detected=int(row['detected'])==1;m['detected']+=detected
 r=[int(row[k])/args.scale for k in ['left','top','right','bottom']];b=label['glyph_box']
 complete=detected and b is not None and r[0]<=b[0] and r[1]<=b[1] and r[2]>=b[2] and r[3]>=b[3]
 m['complete']+=complete
 if complete:m['area_ratios'].append(round(((r[2]-r[0])*(r[3]-r[1]))/((b[2]-b[0])*(b[3]-b[1])),3))
 if label['repeat']==0:m['onsets']+=1;m['complete_onsets']+=complete
 if label['repeat']==0 and (label['background']==2 or (label['positive'] and (not complete or (r[2]-r[0])*(r[3]-r[1])>1.5*(b[2]-b[0])*(b[3]-b[1])))):
  f=frames[n].copy()
  if r[2]>r[0]:cv2.rectangle(f,tuple(map(int,r[:2])),tuple(map(int,r[2:])),(0,255,0),3)
  cv2.putText(f,mode+(' COMPLETE' if complete else ' INCOMPLETE' if label['positive'] else ' NEGATIVE'),(30,50),cv2.FONT_HERSHEY_SIMPLEX,1,(0,255,255),2)
  examples.append(cv2.resize(f,(640,360)))
continuous=[tuple(int(r[k]) for k in ['left','top','right','bottom']) for r,l in zip(rows,labels) if l['mode']=='continuous_two_line']
report={'continuous_two_line_unique_boxes':len(set(continuous)),'scale':args.scale,'cases':summary,'cost_ms':dict(zip(['p50','p95','p99','max'],map(float,np.percentile(cost,[50,95,99,100])))),
 'scope':'Synthetic known glyphs over six real movie frames; not live acceptance. Complete includes all AA glyph pixels.', 'frame_count':len(rows)}
(out/'results.json').write_text(json.dumps(report,indent=2));(out/'labels.json').write_text(json.dumps(labels))
if examples:
 while len(examples)%3:examples.append(np.zeros_like(examples[0]))
 cv2.imwrite(str(out/'contact.jpg'),np.vstack([np.hstack(examples[i:i+3]) for i in range(0,len(examples),3)]))
print(json.dumps(report,indent=2))
