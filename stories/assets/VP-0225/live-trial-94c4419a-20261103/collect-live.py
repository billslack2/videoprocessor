import argparse,json,hashlib,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--process',type=int,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
log=Path(r'C:\Videoprocessor\vp\logs\vp.log')
with log.open('rb') as f:
 f.seek(max(0,log.stat().st_size-16*1024*1024));lines=f.read().decode('utf-8',errors='replace').splitlines()
rows=[]
for line in lines:
 if 'VP presentation decision:' not in line or '{' not in line:continue
 try:r=json.loads(line[line.index('{'):])
 except ValueError:continue
 if r.get('process')==a.process and r.get('submitted'):rows.append(r)
if not rows:raise SystemExit('No current-process submitted telemetry yet')
last=rows[-1];rows=[r for r in rows if r['generation']==last['generation'] and r.get('subtitle_processing_profile')==last.get('subtitle_processing_profile')][-240:]
pairs=[(x,y) for x,y in zip(rows,rows[1:]) if x['selected']!=y['selected']]
s={'process':a.process,'observedUnix':time.time(),'samples':len(rows),'profile':last.get('subtitle_processing_profile'),'mode':last.get('mode'),'firstSource':rows[0]['selected'],'lastSource':last['selected'],'distinctSources':len(set(r['selected'] for r in rows)),'composed':sum(bool(r.get('composed')) for r in rows),'prepared':sum(bool(r.get('source_prepared')) for r in rows),'factsCurrent':sum(bool(r.get('facts_current')) for r in rows),'maxSelectedGap':max((y['selected']-x['selected'] for x,y in pairs),default=0),'maxRecordedSubmitGapMs':max((y['utc_ms']-x['utc_ms'] for x,y in pairs),default=0),'nativeShapes':sorted(set(str(r.get('native')) for r in rows)),'actualShapes':sorted(set(str(r.get('actual_source')) for r in rows)),'reasons':{k:sum(r.get('reason')==k for r in rows) for k in sorted(set(r.get('reason','') for r in rows))},'configurationHash':hashlib.sha256(Path(r'C:\Videoprocessor\vp\VideoProcessor.cfg').read_bytes()).hexdigest(),'scope':'Bounded submitted telemetry, not visual acceptance or full runtime qualification'}
a.output.write_text(json.dumps(dict(s,rows=rows),indent=2)+'\n',encoding='utf-8');print(json.dumps(s,indent=2))
