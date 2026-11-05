"""Export verified RAM-window inputs to the existing index+identity_v1 replay format.

This prepares inputs only; it never runs VP, creates frames, supplies picture
bounds, invents timestamps, or makes a sparse recording into continuous video.
"""
import argparse
import collections
import json
import sys
from pathlib import Path

from inspect_native_ram_recording import digest, inspect

IDENTITY = ('transportGeneration', 'acceptedSequence', 'sourceFrameNumber', 'captureTimestamp',
            'sourceFormatGeneration', 'viewportGeneration', 'rendererGeneration', 'analysisDomainKey',
            'policyGeneration', 'continuityGeneration', 'nearBarDistance', 'optimizationMode',
            'frameDuration100ns', 'discontinuity', 'cadenceRepeat', 'rgbBitAlignment')
CONTEXT = ('transportGeneration', 'sourceFormatGeneration', 'viewportGeneration', 'rendererGeneration',
           'analysisDomainKey', 'policyGeneration', 'continuityGeneration')
OUTCOMES = {'ingress-busy', 'retain-failed', 'ram-evicted', 'duplicate', 'context-changed',
            'invalid-source', 'copy-failed', 'written', 'write-failed', 'cancelled'}


def export(capture, output):
    capture = capture.resolve(strict=True)
    output = output.resolve()
    if output.exists():
        raise ValueError('Output already exists; choose a fresh directory')
    index = capture / 'native-window.jsonl'
    original_index_hash = digest(index)
    rows = [json.loads(line) for line in index.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    if not rows or rows[0].get('kind') != 'limits' or rows[0].get('schema') != 1:
        raise ValueError('Unsupported native window index')
    if any(e.get('outcome') not in OUTCOMES for e in rows[1:]):
        raise ValueError('Unknown or unfinished native event; wait for captureWriting=false')
    for event in rows[1:]:
        if event['outcome'] != 'written':
            continue
        for suffix in ('.bin', '.txt'):
            path = capture / f"frame-{event['index']:06}{suffix}"
            if path.resolve(strict=True).parent != capture:
                raise ValueError('Native input escapes the capture directory')
    verification = inspect(capture)
    if not verification['verifiedWrittenFiles']:
        raise ValueError('Written input verification failed: ' + json.dumps(verification['errors']))
    if not verification['files']:
        raise ValueError('No verified written native inputs')
    files_by_index = {f['index']: f for f in verification['files']}
    frames = []
    for event in rows[1:]:
        if event['outcome'] != 'written':
            continue
        number = event['index']
        binary = capture / f'frame-{number:06}.bin'
        sidecar = binary.with_suffix('.txt')
        lines = sidecar.read_text(encoding='utf-8-sig').splitlines()
        layout = [int(v) for v in lines[0].split()]
        identity = [int(v) for v in lines[1].split()[1:]]
        if len(identity) != 16 or len(layout) != 11 or any(v < 0 for v in identity) or identity[12] <= 0:
            raise ValueError('Replay requires complete original identity_v1 and positive nominal duration')
        metadata = dict(zip(IDENTITY, identity))
        for name in ('frameDuration100ns', 'discontinuity', 'rgbBitAlignment'):
            if metadata[name] != event[name]:
                raise ValueError('Sidecar and event disagree: ' + name)
        if digest(sidecar) != files_by_index[number]['sidecarSHA256']:
            raise ValueError('Sidecar changed during verification')
        frames.append({'index': number, 'file': str(binary), 'sidecar': str(sidecar),
                       'identity_v1': identity, 'identity': metadata, 'layout': layout,
                       'bytes': event['bytes'], 'sha256': files_by_index[number]['nativeSHA256'],
                       'sidecarSha256': files_by_index[number]['sidecarSHA256']})
        if 'captureClock' in files_by_index[number]:
            frames[-1]['captureClock'] = files_by_index[number]['captureClock']
    segments, gaps = [], []
    for frame in frames:
        reasons = []
        if segments:
            previous = segments[-1]['frames'][-1]
            a, b = previous['identity'], frame['identity']
            if any(a[k] != b[k] for k in CONTEXT) or previous['layout'][:7] != frame['layout'][:7] or previous['layout'][10] != frame['layout'][10] or a['rgbBitAlignment'] != b['rgbBitAlignment'] or a['frameDuration100ns'] != b['frameDuration100ns']:
                reasons.append('context-or-layout-change')
            if previous.get('captureClock') != frame.get('captureClock'):
                reasons.append('capture-clock-change')
            if b['discontinuity'] or b['cadenceRepeat']:
                reasons.append('declared-discontinuity-or-repeat')
            if b['acceptedSequence'] != a['acceptedSequence'] + 1:
                reasons.append('accepted-sequence-gap-or-reset')
            if (a['sourceFrameNumber'] or b['sourceFrameNumber']) and b['sourceFrameNumber'] != a['sourceFrameNumber'] + 1:
                reasons.append('source-sequence-gap-or-reset')
            if bool(a['captureTimestamp']) != bool(b['captureTimestamp']) or (a['captureTimestamp'] and b['captureTimestamp'] <= a['captureTimestamp']):
                reasons.append('capture-timestamp-unavailable-or-nonmonotonic')
            omitted = [dict(e) for e in rows[previous['index'] + 1:frame['index']] if e['outcome'] != 'written']
            # Even duplicate/failed offers stay visible; do not silently bridge them.
            if omitted:
                reasons.append('omitted-events-between-inputs')
            if reasons:
                gaps.append({'beforeIndex': previous['index'], 'afterIndex': frame['index'], 'reasons': reasons,
                             'acceptedSequenceDelta': b['acceptedSequence'] - a['acceptedSequence'],
                             'sourceFrameNumberDelta': b['sourceFrameNumber'] - a['sourceFrameNumber'],
                             'captureTimestampDeltaRaw': b['captureTimestamp'] - a['captureTimestamp'],
                             'recordedOmissions': omitted})
        if not segments or reasons:
            segments.append({'number': len(segments) + 1, 'frames': []})
        segments[-1]['frames'].append(frame)
    if digest(index) != original_index_hash:
        raise ValueError('Capture index changed during export')
    recorded_rates = {f.get('captureClock', {}).get('ticksPerSecond') for f in frames}
    common_clock = next(iter(recorded_rates)) if len(recorded_rates) == 1 else None
    manifest = {'schema': 1, 'kind': 'native-ram-replay-inputs', 'captureRoot': str(capture),
                'sourceIndex': str(index), 'sourceIndexSha256': original_index_hash,
                'manifestFormat': 'count then original file index and sixteen identity_v1 integers',
                'timing': {'captureTimestampUnits': 'original renderer input timing-clock ticks',
                           'captureClockTicksPerSecond': common_clock,
                           'clockFrequencyProvenance': 'recorded renderer-bound ITimingClock' if common_clock else 'per-frame metadata; unknown or mixed',
                           'unadjustedHardwareTimeClaim': False,
                           'nominalFrameDurationUnits': '100ns', 'timestampsRewritten': False,
                           'playbackScheduleGenerated': False},
                'pictureBoundsAvailable': rows[0].get('pictureBoundsAvailable', False),
                'semanticSettingsAvailable': rows[0].get('semanticSettingsAvailable', False),
                'pictureOrCaptionAuthoritySupplied': False, 'completeStreamClaim': False,
                'sequenceCoverageUncertain': bool(rows[0].get('unloggedOffers', 0)),
                'limits': rows[0], 'outcomes': dict(collections.Counter(e['outcome'] for e in rows[1:])),
                'frames': frames, 'segments': [], 'gaps': gaps,
                'omissions': [dict(e) for e in rows[1:] if e['outcome'] != 'written'],
                'consumerLimitations': ['Existing renderer fixtures may restrict corpus, dimensions and recorded picture planes; format compatibility alone does not satisfy those assertions.',
                                       'RAM input planes are unknown. Consumers must derive geometry from pixels, never replace zero bounds with a guessed aspect.',
                                       'All inputs retain original hashes and identities. Verify these hashes again before replay. Do not concatenate segments as continuous history.']}
    output.mkdir(parents=True, exist_ok=False)
    def write_list(name, selected):
        path = output / name
        path.write_text(str(len(selected)) + '\n' + ''.join(str(f['index']) + ' ' + ' '.join(map(str, f['identity_v1'])) + '\n' for f in selected))
        return {'path': str(path), 'sha256': digest(path)}
    manifest['captureManifest'] = write_list('capture-manifest.txt', frames)
    for segment in segments:
        selected = segment['frames']
        manifest['segments'].append({'number': segment['number'], 'indices': [f['index'] for f in selected],
                                     'captureTimestampAvailable': all(f['identity']['captureTimestamp'] for f in selected),
                                     'captureClock': selected[0].get('captureClock'),
                                     'manifest': write_list(f"segment-{segment['number']:04}.txt", selected)})
    # Preserve the complete original gap/eviction ledger verbatim, not just a summary.
    (output / 'native-window.jsonl').write_bytes(index.read_bytes())
    (output / 'replay.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        result = export(args.capture, args.output)
    except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps({'replayManifest': str(args.output.resolve() / 'replay.json'),
                      'frames': len(result['frames']), 'segments': len(result['segments']),
                      'gaps': len(result['gaps']), 'completeStreamClaim': False}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
