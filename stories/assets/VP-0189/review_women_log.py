"""Extract sparse log evidence; does not reconstruct unseen source frames."""
from pathlib import Path
import hashlib, json, re

path = Path(r'C:\Users\bslac\Downloads\vp - Kopie.log.txt')
fields = re.compile(r'(?:^|\s)([A-Za-z_][A-Za-z_0-9]*)=("[^"]*"|\S+)')
records = []
for line, text in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
    if ' | Alpha source crop:' not in text and ' | Alpha crop recovery:' not in text: continue
    f = {k: v.strip('"') for k,v in fields.findall(text)}
    records.append(dict(line=line, time=text[:19], kind='crop' if ' | Alpha source crop:' in text else 'recovery', **f))
windows = [('05:27:38','05:28:10',3856,4634), ('05:29:03','05:29:35',5897,6668),
           ('05:34:17','05:34:49',13434,14205), ('05:43:15','05:43:47',7915,8686)]
result = dict(file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
              supplied_passage='Women in Blue S02E06 10:55–11:30', windows=[], recovery_ends=[])
for start,end,first,last in windows:
    rows = [r for r in records if r['kind']=='crop' and start <= r['time'][11:] <= end]
    keep = ['line','time','sequence','applied','rect','fill_rect','owner']
    result['windows'].append(dict(start=start,end=end,source_frames=last-first,
                                  source_seconds=(last-first)*1001/24000,
                                  records=[{k:r.get(k) for k in keep} for r in rows]))
for r in records:
    if r['kind']=='recovery' and r.get('phase')=='end':
        result['recovery_ends'].append({k:r.get(k) for k in ['line','time','generation','event','sequence','duration_ms','proof','candidate_owner','gates']})
result['safe_four_pixel_summaries'] = [{k:r.get(k) for k in ['line','time','sequence','saved','observed','candidate_owner','proof','gate_names']}
    for r in records if r['kind']=='recovery' and r.get('saved')=='0,208-3840,1948' and
    r.get('observed')=='0,208-3840,1952' and r.get('bands_safe')=='1']
out = Path(__file__).parent / 'women-in-blue-evidence.json'
out.write_text(json.dumps(result,indent=2)+'\n', encoding='utf-8')
print(json.dumps({k:result[k] for k in ['sha256','recovery_ends']},indent=2))
print('Safe four-pixel summaries:', len(result['safe_four_pixel_summaries']))
for w in result['windows']: print(w['start'], w['end'], round(w['source_seconds'],3), 'seconds')
