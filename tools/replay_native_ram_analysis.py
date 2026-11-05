"""Replay exported native segments through the production CPU analysis worker.

This is unpaced offline analysis, not a playback, subtitle, GPU, or deadline test.
Every exported continuity segment starts cold. Original gaps remain in the receipt.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from export_native_ram_replay import export
from inspect_native_ram_recording import digest


def run(manifest, executable, output):
    manifest = manifest.resolve(strict=True)
    executable = executable.resolve(strict=True)
    output = output.resolve()
    if output.exists():
        raise ValueError('Output exists; choose a fresh directory')
    original = json.loads(manifest.read_text(encoding='utf-8-sig'))
    if original.get('kind') != 'native-ram-replay-inputs' or original.get('schema') != 1:
        raise ValueError('Expected native RAM export replay.json')
    # Recompute segmentation and provenance from the original ledger, not an edited
    # list of segment boundaries or user-supplied rectangles.
    with tempfile.TemporaryDirectory(prefix='vp-analysis-validate-') as temporary:
        validated = export(Path(original['captureRoot']), Path(temporary) / 'validated')
        for key in ('sourceIndexSha256', 'frames', 'gaps', 'omissions', 'sequenceCoverageUncertain'):
            if original[key] != validated[key]:
                raise ValueError('Export no longer matches original capture: ' + key)
        if [s['indices'] for s in original['segments']] != [s['indices'] for s in validated['segments']]:
            raise ValueError('Export continuity segments were changed')
        for item in [original['captureManifest'], *(s['manifest'] for s in original['segments'])]:
            if digest(Path(item['path'])) != item['sha256']:
                raise ValueError('Export manifest hash mismatch')
        manifest_hash, exe_hash = digest(manifest), digest(executable)
        output.mkdir(parents=True)
        (output / 'original-replay.json').write_bytes(manifest.read_bytes())
        (output / 'native-window.jsonl').write_bytes(Path(validated['sourceIndex']).read_bytes())
        frames = {f['index']: f for f in validated['frames']}
        results, all_rows = [], []
        for segment in validated['segments']:
            number = segment['number']
            selected = [frames[i] for i in segment['indices']]
            prefix = output / f'segment-{number:04}'
            inputs, pins, ledger = prefix.with_suffix('.inputs.txt'), prefix.with_suffix('.pins.txt'), prefix.with_suffix('.jsonl')
            inputs.write_text(str(len(selected)) + '\n' + ''.join(str(f['index']) + ' ' + ' '.join(map(str, f['identity_v1'])) + '\n' for f in selected))
            pins.write_text(''.join(f"{f['index']} {f['sha256']} {f['sidecarSha256']}\n" for f in selected))
            process = subprocess.run([str(executable), validated['captureRoot'], str(inputs), str(pins), str(ledger)],
                                     capture_output=True, text=True)
            prefix.with_suffix('.stdout.txt').write_text(process.stdout)
            prefix.with_suffix('.stderr.txt').write_text(process.stderr)
            rows = [json.loads(line) for line in ledger.read_text().splitlines()] if ledger.exists() else []
            actual = [r for r in rows if r['kind'] == 'frame']
            exact = len(actual) == len(selected) and all(
                r['index'] == f['index'] and r['identity_v1'] == f['identity_v1']
                and r.get('completed') and r.get('exactCurrentIdentity')
                and (not r.get('supportVerified') or r.get('supportExactCurrentIdentity')) for r, f in zip(actual, selected))
            results.append({'number': number, 'indices': segment['indices'], 'exitCode': process.returncode,
                            'exactOriginalSourcesCompleted': exact, 'ledger': str(ledger),
                            'ledgerSha256': digest(ledger) if ledger.exists() else None,
                            'summary': next((r for r in rows if r['kind'] == 'summary'), None)})
            for row in actual:
                all_rows.append(dict(row, segment=number))
            if process.returncode or not exact:
                break
        stable = (digest(manifest) == manifest_hash and digest(executable) == exe_hash
                  and digest(Path(validated['sourceIndex'])) == validated['sourceIndexSha256']
                  and digest(output / 'native-window.jsonl') == validated['sourceIndexSha256'])
        complete = stable and len(results) == len(validated['segments']) and all(
            r['exitCode'] == 0 and r['exactOriginalSourcesCompleted'] for r in results)
        (output / 'frames.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in all_rows))
        receipt = {'schema': 1, 'kind': 'native-ram-production-worker-replay', 'passed': complete,
                   'manifest': str(manifest), 'manifestSha256': manifest_hash,
                   'executable': str(executable), 'executableSha256': exe_hash, 'producerFilesStable': stable,
                   'segments': results, 'inputFrames': len(frames), 'completedFrames': len(all_rows),
                   'gaps': validated['gaps'], 'omissions': validated['omissions'],
                   'sequenceCoverageUncertain': validated['sequenceCoverageUncertain'],
                   'mode': 'unpaced serial CPU analysis; cold at each exported continuity segment',
                   'completeStreamClaim': False, 'videoDeadlineClaim': False, 'gpu': False,
                   'subtitleCompositionTested': False, 'acceptedSceneStateModeled': False,
                   'outputCropApplied': False, 'expectedGeometryInvented': False,
                   'notes': ['Unknown or rejected detector results are observations, not test failures.',
                             'No source frame, timestamp, sidecar plane or expected ratio is synthesized.',
                             'Timing includes production worker work, separately reported input hashing and queue cost; no realtime replay schedule is generated.',
                             'Executable hash identifies the tested producer. Record the corresponding build/library receipt separately.']}
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
        return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        receipt = run(args.manifest, args.executable, args.output)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 2
    print(json.dumps({'passed': receipt['passed'], 'frames': receipt['completedFrames'],
                      'segments': len(receipt['segments']), 'receipt': str(args.output / 'receipt.json')}))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
