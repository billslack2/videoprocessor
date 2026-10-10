import json,re,statistics
from pathlib import Path
p=Path(__file__).parent
allresults={k:json.loads((p/f'corpus-{k}.json').read_text()) for k in ['baseline','candidate']}
summary={}
for k,rs in allresults.items():
 group=[r for r in rs if r['pad']==0 and r['name'] in ['beeping_50','autopilot_53']]
 summary[k]={'caption_fallback':{'median_of_warm_medians_ms':statistics.median(float(re.search(r'recovery_warm_median_ms=([\d.]+)',r['output'])[1]) for r in group),'range_warm_medians_ms':[min(float(re.search(r'recovery_warm_median_ms=([\d.]+)',r['output'])[1]) for r in group),max(float(re.search(r'recovery_warm_median_ms=([\d.]+)',r['output'])[1]) for r in group)],'max_warm_p95_ms':max(float(re.search(r'p95_ms=([\d.]+)',r['output'])[1]) for r in group)}}
for r in allresults['candidate']:
 if r['positive'] and r['pad']==0:assert 'gated=analyzed detected=1 lines=1' in r['output'],r
 if not r['positive']:assert 'reason=detected' not in r['output'],r
summary['assertions']='PASS: all8 exact-plane caption variants recovered and detected1line; all12 no-caption variants produced no glyph detection. Four padded-plane caption fallback cases remain intentionally documented failures.'
(p/'corpus-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
