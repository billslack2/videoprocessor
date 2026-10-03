"""Replay labelled synthetic captions over real movie frames through C++.
Requires numpy/opencv and the built SubtitleBoxProbe.exe. No OCR/model.
"""
import argparse, csv, json, subprocess
from pathlib import Path
import cv2
import numpy as np
ap=argparse.ArgumentParser()
ap.add_argument('--video',required=True);ap.add_argument('--probe',required=True)
ap.add_argument('--output',required=True);ap.add_argument('--scale',type=int,default=1)
ap.add_argument('--source-rect',type=int,nargs=4,metavar=('LEFT','TOP','WIDTH','HEIGHT'),
 help='Optional movie-only background region in a desktop recording; originals are unchanged')
ap.add_argument('--sample-seconds',type=float,nargs=6,default=[3,7,14,23,34,43],
 help='Six background timestamps; choose clean movie frames without menus or subtitles')
args=ap.parse_args()
if args.scale not in [1,2]:raise ValueError('scale must be 1 or 2')
W,H=1920*args.scale,1080*args.scale
picture_top,picture_bottom=120*args.scale,960*args.scale
out=Path(args.output);out.mkdir(parents=True,exist_ok=True)
cap=cv2.VideoCapture(args.video)
backgrounds=[]
for seconds in args.sample_seconds:
 if seconds<0:raise ValueError('sample-seconds cannot be negative')
 cap.set(cv2.CAP_PROP_POS_MSEC,seconds*1000);ok,f=cap.read()
 if not ok: raise RuntimeError('Cannot read reference frame')
 if args.source_rect:
  left,top,width,height=args.source_rect
  if left<0 or top<0 or width<=0 or height<=0 or left+width>f.shape[1] or top+height>f.shape[0]:
   raise ValueError('source-rect must lie inside every decoded frame')
  f=f[top:top+height,left:left+width]
 backgrounds.append(cv2.resize(f,(W,H)))
cap.release()
frames=[];labels=[]
def text(f,mask,value,xy):
 xy=tuple(int(round(v*args.scale)) for v in xy)
 font_scale=1.1*args.scale
 cv2.putText(f,value,xy,cv2.FONT_HERSHEY_SIMPLEX,font_scale,(0,0,0),6*args.scale,cv2.LINE_AA)
 cv2.putText(f,value,xy,cv2.FONT_HERSHEY_SIMPLEX,font_scale,(240,240,240),2*args.scale,cv2.LINE_AA)
 cv2.putText(mask,value,xy,cv2.FONT_HERSHEY_SIMPLEX,font_scale,255,2*args.scale,cv2.LINE_AA)
def centered_text(f,mask,value,baseline):
 width=cv2.getTextSize(value,cv2.FONT_HERSHEY_SIMPLEX,1.1*args.scale,2*args.scale)[0][0]
 text(f,mask,value,(((W-width)//2)/args.scale,baseline))
def bounds(mask):
 yy,xx=np.nonzero(mask)
 return [int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)] if len(xx) else None
negative_modes={'blank','picture','menu','left_title','right_title'}
for mode in ['blank','topbar','topcross','bottomcross','bottomtwo','upper_crossing_two',
             'bar','short','two_glyph','picture','menu','left_title','right_title',
             'left_title_with_subtitle','right_title_with_subtitle']:
 for i,bg in enumerate(backgrounds):
  f=bg.copy();f[:picture_top]=0;f[picture_bottom:]=0;mask=np.zeros((H,W),np.uint8)
  distractor=np.zeros_like(mask)
  if mode=='topbar':text(f,mask,'TOP BAR SUBTITLE',(740,70))
  if mode=='topcross':text(f,mask,'TOP BOUNDARY SUBTITLE',(700,138))
  if mode=='bottomcross':text(f,mask,'BOTTOM BOUNDARY SUBTITLE',(640,978))
  if mode=='bottomtwo':
   text(f,mask,'THE UPPER LINE IS WIDER AND IN THE PICTURE',(520,937))
   text(f,mask,'BOTTOM LINE',(780,978))
  if mode=='upper_crossing_two':
   # The wider/stronger upper line itself intersects the bar. Both lines
   # must be enclosed even though the detector therefore anchors on top.
   centered_text(f,mask,'SPRITE: You better tell him',970)
   centered_text(f,mask,'the truth, then.',1008)
  if mode=='bar':text(f,mask,'SUBTITLE ENTIRELY IN THE BAR',(630,1030))
  if mode=='short':text(f,mask,'No.',(915,978))
  if mode=='two_glyph':centered_text(f,mask,'Hi',978)
  if mode=='picture':text(f,mask,'PICTURE ONLY',(750,900))
  if mode=='menu':text(f,mask,'OPTIONS       SETTINGS       EXIT',(550,946))
  if 'title' in mode:
   title='Eternals'
   width=cv2.getTextSize(title,cv2.FONT_HERSHEY_SIMPLEX,1.1*args.scale,2*args.scale)[0][0]/args.scale
   left=70 if mode.startswith('left') else 1920-70-width
   text(f,distractor,title,(left,70))
   if mode.endswith('_with_subtitle'):centered_text(f,mask,'No.',978)
  positive=mode not in negative_modes
  # Test onset directly after a clean frame; then repeated cue continuity.
  blank=bg.copy();blank[:picture_top]=0;blank[picture_bottom:]=0
  for _ in range(3):frames.append(blank);labels.append({'mode':'gap','positive':False,'glyph_box':None})
  box=bounds(mask) if positive else None
  expected_lines=2 if mode in ['bottomtwo','upper_crossing_two'] else 1 if positive else 0
  for repeat in range(3):frames.append(f);labels.append({'mode':mode,'background':i,'repeat':repeat,
   'positive':positive,'glyph_box':box,'expected_lines':expected_lines,'distractor_box':bounds(distractor)})
# Same cue across scene changes must retain a tight, constant rectangle.
for repeat in range(3):
 for i,bg in enumerate(backgrounds):
  f=bg.copy();f[:picture_top]=0;f[picture_bottom:]=0;mask=np.zeros((H,W),np.uint8)
  text(f,mask,'THE UPPER LINE IS WIDER AND IN THE PICTURE',(520,937));text(f,mask,'BOTTOM LINE',(780,978))
  yy,xx=np.nonzero(mask);box=[int(xx.min()),int(yy.min()),int(xx.max()+1),int(yy.max()+1)]
  frames.append(f);labels.append(dict(mode='continuous_two_line',background=i,repeat=repeat,positive=True,glyph_box=box,expected_lines=2))
with (out/'frames.csv').open('w',newline='') as result:
 proc=subprocess.Popen([args.probe,str(W),str(H),str(picture_top),str(picture_bottom)],stdin=subprocess.PIPE,stdout=result)
 for f in frames:
  # Bars and glyphs are drawn at target resolution after resizing backgrounds.
  # Resizing a completed captioned frame would leak scenery into the first bar
  # row and invalidate the known picture boundary supplied to the detector.
  ycc=cv2.cvtColor(f,cv2.COLOR_BGR2YCrCb)
  y=ycc[:,:,0].astype(np.uint16)
  uv=cv2.resize(ycc[:,:,1:],(W//2,H//2),interpolation=cv2.INTER_AREA)[:,:,::-1].copy().astype(np.uint16)
  # Full-range ten-bit luma and 4:2:0 interleaved CbCr in P010 storage.
  proc.stdin.write(((y*4)<<6).tobytes());proc.stdin.write(((uv*4)<<6).tobytes())
 proc.stdin.close()
 if proc.wait()!=0:raise RuntimeError('probe failed')
rows=list(csv.DictReader((out/'frames.csv').open()))
if len(rows)!=len(labels):raise RuntimeError(f'Probe returned {len(rows)} rows for {len(labels)} frames')
summary={};cost=[];examples=[]
for n,(row,label) in enumerate(zip(rows,labels)):
 cost.append(float(row['cost_ms']))
 if label['mode']=='gap':continue
 mode=label['mode'];m=summary.setdefault(mode,dict(expected_detected=label['positive'],frames=0,detected=0,
  complete=0,correct=0,false_positives=0,onsets=0,complete_onsets=0,area_ratios=[]))
 m['frames']+=1;detected=int(row['detected'])==1;m['detected']+=detected
 r=[int(row[k]) for k in ['left','top','right','bottom']];b=label['glyph_box']
 complete=detected and b is not None and int(row['lines'])==label['expected_lines'] and r[0]<=b[0] and r[1]<=b[1] and r[2]>=b[2] and r[3]>=b[3]
 m['complete']+=complete
 ratio=((r[2]-r[0])*(r[3]-r[1]))/((b[2]-b[0])*(b[3]-b[1])) if complete else None
 if complete:m['area_ratios'].append(round(ratio,3))
 has_box=r[2]>r[0] and r[3]>r[1]
 false_positive=not label['positive'] and (detected or has_box)
 m['false_positives']+=false_positive
 # Completeness alone would accept a giant box enclosing a title and subtitle.
 correct=complete and ratio<=1.5 if label['positive'] else not false_positive
 m['correct']+=correct
 if label['repeat']==0:m['onsets']+=1;m['complete_onsets']+=complete
 if label['repeat']==0 and (label['background']==2 or not correct):
  f=frames[n].copy()
  if r[2]>r[0]:cv2.rectangle(f,tuple(map(int,r[:2])),tuple(map(int,r[2:])),(0,255,0),3*args.scale)
  cv2.putText(f,mode+(' PASS' if correct else ' FAIL'),(30*args.scale,50*args.scale),cv2.FONT_HERSHEY_SIMPLEX,args.scale,(0,255,255),2*args.scale)
  examples.append(cv2.resize(f,(640,360)))
continuous=[tuple(int(r[k]) for k in ['left','top','right','bottom']) for r,l in zip(rows,labels) if l['mode']=='continuous_two_line']
report={'continuous_two_line_unique_boxes':len(set(continuous)),'scale':args.scale,'cases':summary,'cost_ms':dict(zip(['p50','p95','p99','max'],map(float,np.percentile(cost,[50,95,99,100])))),
 'scope':'Synthetic known glyphs over six real movie frames; not live or lookahead acceptance. Complete includes all AA glyph pixels and expected line count; correct also requires area ratio <= 1.5.',
 'source_video':str(Path(args.video).resolve()),'source_rect':args.source_rect,
 'sample_seconds':args.sample_seconds,'frame_count':len(rows),
 'fixture_generation':'Background resized first; bars and antialiased text drawn at target resolution. Glyph labels use source-raster coordinates.'}
(out/'results.json').write_text(json.dumps(report,indent=2));(out/'labels.json').write_text(json.dumps(labels))
if examples:
 while len(examples)%3:examples.append(np.zeros_like(examples[0]))
 cv2.imwrite(str(out/'contact.jpg'),np.vstack([np.hstack(examples[i:i+3]) for i in range(0,len(examples),3)]))
print(json.dumps(report,indent=2))
