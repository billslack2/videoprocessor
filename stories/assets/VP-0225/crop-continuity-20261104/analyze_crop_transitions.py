import argparse, collections, json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--log',type=Path,required=True);p.add_argument('--process',type=int,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
rows=[]
for line in a.log.open(errors='replace'):
    if 'VP presentation decision:' not in line:continue
    try:r=json.loads(line[line.index('{'):])
    except (ValueError,json.JSONDecodeError):continue
    if r.get('process')==a.process:rows.append(r)
def context(r):return tuple(str(r.get(k)) for k in ['instance','generation','format','domain','policy','continuity','viewport','subtitle_processing_profile'])
def inside(r,n):return r[0]>=n[0] and r[1]>=n[1] and r[2]<=n[2] and r[3]<=n[3]
changes=[];onoff=0;stable=0
for x,y in zip(rows,rows[1:]):
    if not(x['submitted'] and y['submitted'] and x['facts_current'] and y['facts_current'] and x['native_available'] and y['native_available'] and context(x)==context(y) and y['selected']==x['selected']+1 and x['native']==y['native'] and x['subtitle_processing_profile']==3):continue
    different=x['detected']!=y['detected'];onoff+=different
    if x['actual_source']==y['actual_source'] and x['actual_target']==y['actual_target']:stable+=different;continue
    xr,yr,n=x['actual_source'],y['actual_source'],x['native']
    sameY=(xr[1],xr[3])==(yr[1],yr[3])==(n[1],n[3])
    kind='horizontal-fill-pulse' if sameY and inside(xr,n) and inside(yr,n) and (xr[0],xr[2])!=(yr[0],yr[2]) else 'expanded-envelope-change' if not inside(xr,n) or not inside(yr,n) else 'other'
    changes.append({'kind':kind,'before':x,'after':y,'detectionOnOff':different,'sourceDelta':[j-i for i,j in zip(xr,yr)],'targetDelta':[j-i for i,j in zip(x['actual_target'],y['actual_target'])]})
summary={'process':a.process,'firstTimecode':rows[0]['timecode'],'lastTimecode':rows[-1]['timecode'],'allRows':len(rows),'scope':'Consecutive current/submitted Reconstruction pairs with identical context/native bounds. Settings snapshot is separate.','sameNativeTransitions':len(changes),'kinds':dict(collections.Counter(c['kind'] for c in changes)),'detectedOnOffPairs':onoff,'detectedOnOffStableGeometry':stable,'detectedOnOffChangedGeometry':onoff-stable,'transitions':changes}
(a.output/'crop-transition-summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
cases=[]
for label,seqs in [('pure-fill-unprocessed-candidate',[1413,1414,1415]),('pure-fill-cue-handoff',[1329,1330,1331]),('expanded-pending-cue78',[5298,5299,5300])]:
    selected=[]
    for seq in seqs:
        found=[r for r in rows if r['instance'].startswith('{4368DA22') and r['generation']==2 and r['selected']==seq]
        assert len(found)==1,(label,seq,len(found));selected.append(found[0])
    cases.append({'label':label,'rows':selected})
(a.output/'repro-cases.json').write_text(json.dumps(cases,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k!='transitions'},indent=2))
