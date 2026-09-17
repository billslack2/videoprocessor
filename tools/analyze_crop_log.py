"""Summarize logged crop/layout transitions without treating sparse logs as frames."""
import argparse
from collections import Counter, defaultdict
import json
import re

FIELDS = re.compile(r'(?:^|\s)([A-Za-z_][A-Za-z_0-9]*)=("[^"]*"|\S+)')


def analyze(lines, start='', end='9999'):
    counts = Counter()
    owners = Counter()
    gates = Counter()
    changes = []
    layout_changes = []
    events = {}
    previous = {}
    sequence_by_kind = {}
    sessions = defaultdict(int)
    diagnostics = 0
    for number, text in enumerate(lines, 1):
        timestamp = text[:19]
        if len(timestamp) != 19 or timestamp[4] != '-':
            continue
        if ' | ' not in text:
            continue
        message = text.split(' | ', 1)[1]
        kind = message.split(':', 1)[0]
        if kind not in ('Alpha source crop', 'Alpha crop recovery', 'Alpha final layout', 'Alpha crop edge trace'):
            continue
        f = {k: v.strip('"') for k, v in FIELDS.findall(message)}
        sequence = int(f.get('sequence', 0))
        generation = f.get('generation', f.get('frame_generation', '?'))
        # A renderer restart can reuse generation=1. Never connect its state
        # to the old instance merely because the number matches.
        last_sequence = sequence_by_kind.get(kind, 0)
        if sequence and sequence < last_sequence:
            sessions[kind] += 1
            previous.pop(kind, None)
        sequence_by_kind[kind] = sequence
        context = (sessions[kind], generation)
        inside = start <= timestamp <= end
        if kind == 'Alpha source crop':
            if 'applied' not in f:
                if inside: counts['crop_records_without_applied'] += 1
                continue
            state = (f['applied'], f.get('rect'), f.get('fill_applied'), f.get('fill_rect'))
            old = previous.get(kind)
            if inside:
                counts['crop_records'] += 1
                owners[f.get('owner', 'unavailable')] += 1
                if old and old[0] == context and old[1] != state:
                    counts['source_geometry_changes'] += 1
                    counts['applied_changes'] += old[1][0] != state[0]
                    changes.append(dict(line=number, time=timestamp, session=context[0], generation=generation,
                                        sequence=sequence, applied=f['applied'], rect=f.get('rect'),
                                        fill_applied=f.get('fill_applied'), fill_rect=f.get('fill_rect'),
                                        owner=f.get('owner'), scene=f.get('scene_event'), reason=f.get('reason')))
            previous[kind] = context, state
        elif kind == 'Alpha final layout':
            state = tuple(f.get(k) for k in ('presentation', 'picture', 'screen', 'mapping',
                                             'subtitle_shift_source_pixels', 'anamorphic'))
            old = previous.get(kind)
            if inside:
                counts['layout_records'] += 1
                if old and old[0] == context and old[1] != state:
                    counts['layout_changes'] += 1
                    layout_changes.append(dict(line=number, time=timestamp, sequence=sequence,
                                               presentation=f.get('presentation'), picture=f.get('picture'),
                                               mapping=f.get('mapping')))
            previous[kind] = context, state
        elif inside and kind == 'Alpha crop recovery':
            diagnostics += 1
            gate_names = f.get('gate_names', 'unavailable')
            gates.update(x for x in gate_names.split(',') if x != 'none')
            # Older diagnostics reused an ended event id for ordinary changes.
            # Those records must not overwrite the completed event's duration.
            phase = f.get('phase')
            if f.get('event') == '0' or (phase not in ('start', 'end', 'summary') and
                    f.get('recovery') != '1' and f.get('episode') != 'full-raster'):
                continue
            key = '/'.join([str(context[0]), generation, f.get('epoch', '?'), f.get('event', '?')])
            event = events.setdefault(key, {'first_record': number, 'first_time': timestamp, 'records': 0})
            event.update(last_record=number, last_time=timestamp, last_phase=f.get('phase'),
                         duration_ms=f.get('duration_ms'), applied_changes=f.get('applied_changes'),
                         evidence_flips=f.get('evidence_flips'), proof_resets=f.get('proof_resets'),
                         last_gates=gate_names, proof=f.get('proof'), episode_proof=f.get('episode_proof'),
                         candidate_reason=f.get('candidate_reason'), sampling_reaffirmed=f.get('sampling_reaffirmed'),
                         inspection_latched=f.get('inspection_latched'), horizontal_bounded=f.get('horizontal_bounded'))
            event['records'] += 1
        elif inside and kind == 'Alpha crop edge trace':
            counts['edge_trace_records'] += 1
    limitations = ['Logs are change-triggered/rate-limited, not a complete consecutive-frame or pixel trace.',
                   'Counts describe logged source-crop and final-layout changes, not proof of a visible symptom.',
                   'capture_missed is inferred from hardware timestamp gaps; it does not independently prove lost delivery.']
    if not diagnostics:
        limitations.append('Recovery gate/proof diagnostics unavailable in this log; exact blocked gates cannot be inferred.')
    if not counts['edge_trace_records']:
        limitations.append('Detailed sampled edge evidence unavailable; outside pixels cannot be identified as stars or subtitles.')
    return dict(interval={'start': start or None, 'end': None if end == '9999' else end},
                counts=dict(counts), owners=dict(owners), diagnostic_gate_record_counts=dict(gates),
                events=events, source_changes=changes, layout_changes=layout_changes, limitations=limitations)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    parser.add_argument('--start', default='', help='Inclusive YYYY-MM-DD HH:MM:SS in log time')
    parser.add_argument('--end', default='9999', help='Inclusive YYYY-MM-DD HH:MM:SS in log time')
    parser.add_argument('--output', help='Optional JSON output; otherwise stdout')
    args = parser.parse_args()
    with open(args.log, encoding='utf-8-sig', errors='replace') as f:
        result = analyze(f, args.start, args.end)
    output = json.dumps(result, indent=2)
    if args.output:
        with open(args.output, 'w', encoding='utf-8') as f: f.write(output+'\n')
    else:
        print(output)


if __name__ == '__main__':
    main()
