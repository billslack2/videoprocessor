"""Read-only crop log analysis; exact field boundaries avoid shift_applied matches."""
import collections
import datetime as dt
import hashlib
import json
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
lines = path.read_text(encoding='utf-8-sig', errors='replace').splitlines()
field_re = re.compile(r'(?:^|\s)([A-Za-z_][A-Za-z_0-9]*)=("[^"]*"|\S+)')
records = []
for number, line in enumerate(lines, 1):
    if len(line) < 22 or line[4:5] != '-':
        continue
    records.append({'line': number, 'time': line[:19], 'message': line[22:],
                    'fields': {k: v.strip('"') for k, v in field_re.findall(line)}})
crops = [r for r in records if r['message'].startswith('Alpha source crop:')]
episodes = [r for r in records if r['message'].startswith('Alpha near-black presentation episode:')]
changes = []
previous = None
for r in crops:
    f = r['fields']
    key = tuple(f.get(k) for k in ('applied', 'rect', 'fill_applied', 'fill_rect'))
    if previous and key != previous[0]:
        changes.append({**r, 'previous': previous[1]['fields'], 'applied_changed': key[0] != previous[0][0]})
    previous = key, r

def window(rs, start, end):
    return [r for r in rs if start <= r['time'] <= end]

def counts(rs, key):
    return dict(collections.Counter(r['fields'].get(key, '<missing>') for r in rs))

windows = [('flicker', '2026-09-16 00:30:08', '2026-09-16 00:30:39'),
           ('credits', '2026-09-16 00:35:28', '2026-09-16 00:39:36'),
           ('late-burst', '2026-09-16 00:39:17', '2026-09-16 00:39:18'),
           ('latch-54s', '2026-09-15 23:00:11', '2026-09-15 23:01:05')]
summary = {'file': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
           'lines': len(lines), 'crop_records': len(crops), 'windows': {}}
for name, start, end in windows:
    cs = window(crops, start, end)
    ch = window(changes, start, end)
    eps = window(episodes, start, end)
    summary['windows'][name] = {
        'start': start, 'end': end, 'records': len(cs), 'geometry_changes': len(ch),
        'applied_changes': sum(x['applied_changed'] for x in ch),
        'fields': {k: counts(cs, k) for k in ['applied', 'rect', 'fill_applied', 'fill_rect', 'owner',
                    'near_black', 'near_black_episode', 'retention_safe', 'translated', 'expanded',
                    'shift_applied', 'screen_aspect', 'cut', 'scene_event', 'latest_trusted',
                    'latest_evidence', 'geometry_generation', 'frame_generation', 'dense_scan']},
        'episode_events': eps,
        'changes': [{k: r[k] for k in ['line', 'time', 'fields', 'previous', 'applied_changed']} for r in ch]
    }
    chosen = window(records, start, end)
    (out / (name + '-excerpt.txt')).write_text('\n'.join(f"{r['line']}: {lines[r['line']-1]}" for r in chosen)+'\n', encoding='utf-8')
viewing_start = '2026-09-15 22:31:00'
summary['viewing_latches'] = [r for r in episodes if r['time'] >= viewing_start and r['fields'].get('to_full') == '1']
summary['end_session_types'] = dict(collections.Counter(re.split(r': | = | sequence=',r['message'])[0] for r in records if r['time'] >= '2026-09-16 00:35:00'))
(out/'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
with (out/'geometry-changes.tsv').open('w', encoding='utf-8') as f:
    f.write('line\ttime\tsequence\tapplied\trect\tfill_rect\towner\tretention_safe\tnear_black_episode\treason\n')
    for r in changes:
        f.write('\t'.join([str(r['line']),r['time']] + [r['fields'].get(k, '') for k in ['sequence','applied','rect','fill_rect','owner','retention_safe','near_black_episode','reason']])+'\n')
for name, value in summary['windows'].items():
    print(name, json.dumps({k:v for k,v in value.items() if k not in ['changes','episode_events']}, indent=2))
print('LATCHES', '\n'.join(f"{r['line']} {r['time']} {r['fields']}" for r in summary['viewing_latches']))
