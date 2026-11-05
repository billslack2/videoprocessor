"""Opt-in isolated renderer replay of verified native RAM capture segments.

Requires explicit original source clock frequency and profile. Does not launch VP.
Uses the existing test-only renderer host and production plugin factory.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from export_native_ram_replay import export
from inspect_native_ram_recording import digest

PROFILES = ('off', 'classic', 'black', 'reconstruction')


def prepare(manifest, output, clock_hz, profile, screen_aspect, preload_mib=0):
    if not 0 < clock_hz <= 1000000000:
        raise ValueError('Explicit source clock Hz is required, between 1 and 1e9')
    if not 0 <= preload_mib <= 16384:
        raise ValueError('Native preload MiB must be between 0 and 16384; zero streams from disk')
    if profile not in PROFILES or not 1 <= screen_aspect <= 4:
        raise ValueError('Invalid explicit presentation profile/screen aspect')
    manifest = manifest.resolve(strict=True)
    output = output.resolve()
    if output.exists():
        raise ValueError('Choose a fresh output directory')
    original = json.loads(manifest.read_text(encoding='utf-8-sig'))
    if original.get('kind') != 'native-ram-replay-inputs' or original.get('schema') != 1:
        raise ValueError('Expected exported native RAM replay.json')
    with tempfile.TemporaryDirectory(prefix='vp-renderer-revalidate-') as temporary:
        valid = export(Path(original['captureRoot']), Path(temporary) / 'validated')
        for key in ('frames', 'sourceIndexSha256', 'gaps', 'omissions', 'sequenceCoverageUncertain'):
            if original[key] != valid[key]:
                raise ValueError('Original capture changed: ' + key)
        if [s['indices'] for s in original['segments']] != [s['indices'] for s in valid['segments']]:
            raise ValueError('Original continuity segmentation changed')
    frames = {f['index']: f for f in valid['frames']}
    for f in frames.values():
        l, identity = f['layout'], f['identity_v1']
        if not (0 < l[0] <= 8192 and 0 < l[1] <= 4320 and l[2] == ((l[0] + 47) // 48) * 128
                and l[3:7] == [0, 3, 3, 4] and l[10] == 1):
            raise ValueError('Initial renderer replay supports limited-range SDR REC709 V210 only')
        if not (identity[2] and 0 < identity[3] < 2**63 - clock_hz*124 and 0 < identity[12] <= 10000000):
            raise ValueError('Missing or unsupported original source counter/timing')
    for s in valid['segments']:
        selected = [frames[i] for i in s['indices']]
        if len(selected) > 4096 or selected[-1]['identity_v1'][3] - selected[0]['identity_v1'][3] > clock_hz*120:
            raise ValueError('Renderer segment exceeds 4096 frames or 120 seconds')
    output.mkdir(parents=True)
    (output / 'original-replay.json').write_bytes(manifest.read_bytes())
    (output / 'native-window.jsonl').write_bytes(Path(valid['sourceIndex']).read_bytes())
    config = output / 'isolated-profile.cfg'
    kind = {'reconstruction': 'generated_gray'}.get(profile, profile)
    config.write_text('[general]\npersist_profile_selection: false\nswitch_refresh_rate: never\n'
                      '[vprenderer]\nquality: fast\n'
                      f'[vprenderer.viewport.scope]\nscreen_aspect: {screen_aspect}:1\n'
                      '[vprenderer.zoom.scope]\nautomatic_crop: true\nsubtitle_fit: true\n'
                      f'[vprenderer.subtitles.{profile}]\ntype: {kind}\nshortcut: F5\n'
                      'offset_pixels: 10\nsubtitle_hold_ms: 250\nsubtitle_near_bar_px: 20\n'
                      'subtitle_script_hint: latin\ncleanup_fill: stable_surround\n'
                      'minimum_headroom_pixels: 30\ntext_reduction_percent: 30\nbackground_edge: extend\n')
    segments = []
    for segment in valid['segments']:
        directory = output / f"segment-{segment['number']:04}"
        directory.mkdir()
        selected = [frames[i] for i in segment['indices']]
        inputs, pins = directory / 'inputs.txt', directory / 'pins.txt'
        inputs.write_text(str(len(selected)) + '\n' + ''.join(str(f['index']) + ' ' + ' '.join(map(str, f['identity_v1'])) + '\n' for f in selected))
        pins.write_text(''.join(f"{f['index']} {f['sha256']} {f['sidecarSha256']}\n" for f in selected))
        segments.append(dict(number=segment['number'], directory=str(directory), inputs=str(inputs), pins=str(pins), frames=selected))
    plan = dict(schema=1, manifest=str(manifest), manifestSha256=digest(manifest), captureRoot=valid['captureRoot'],
                sourceIndexSha256=valid['sourceIndexSha256'], clockHz=clock_hz, clockFrequencyProvenance='explicit caller input; verify from capture device/build',
                profile=profile, screenAspect=screen_aspect, preloadMiB=preload_mib, config=str(config), configSha256=digest(config),
                segments=segments, gaps=valid['gaps'], omissions=valid['omissions'],
                sequenceCoverageUncertain=valid['sequenceCoverageUncertain'],
                sourcePixelsOrTimestampsRewritten=False, sourceReplayLoops=0, acquisitionPrerollFrames=0,
                runtimeTransportIdentityRecreated=True, originalSemanticSettingsSupplied=False,
                sidecarPicturePlanesSupplied=False, expectedAspectSupplied=False, completeStreamClaim=False,
                scope='SDR V210 original source pixels/counters/timestamps; fresh transport generation and accepted IDs mapped by exact original counter/tick pair; cold renderer at each recorded segment')
    (output / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n')
    return plan


def gpu_sample():
    result = subprocess.run(['nvidia-smi', '--query-gpu=index,utilization.gpu,memory.used,power.draw', '--format=csv,noheader,nounits'],
                            text=True, capture_output=True, timeout=15, check=True)
    rows = list(csv.reader(result.stdout.splitlines()))
    if not rows:
        raise ValueError('GPU idle state unavailable')
    return [{'index': int(r[0]), 'utilization': float(r[1]), 'memoryMiB': float(r[2]), 'powerW': float(r[3])} for r in rows]


def require_closed_and_idle():
    tasks = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq VideoProcessor.exe', '/FO', 'CSV', '/NH'], text=True, capture_output=True, check=True)
    if any('videoprocessor.exe' in line.lower() for line in tasks.stdout.splitlines()):
        raise ValueError('VideoProcessor is running; no isolated GPU replay started')
    samples = []
    for _ in range(3):
        sample = gpu_sample()
        samples.append(sample)
        if any(r['utilization'] > 5 for r in sample):
            raise ValueError('GPU is not idle; defer without stopping other applications')
        time.sleep(1)
    return samples


def describe_omissions(annotations):
    """Observed ordering only; queue-disposition counts remain separate evidence."""
    outputs = [(n, r) for n, a in enumerate(annotations) for r in a['submissions']]
    first_wall = min((r.get('wallNs', float('inf')) for _, r in outputs), default=None)
    last_ordinal = max((n for n, _ in outputs), default=None)
    missing = []
    for n, a in enumerate(annotations):
        if a['submissions']:
            continue
        offered = a.get('input')
        if not offered:
            category = 'not-offered'
        elif not outputs:
            category = 'no-submission-in-segment'
        elif n > last_ordinal:
            category = 'after-last-submitted-source'
        elif offered.get('offeredWallNs') is not None and offered['offeredWallNs'] < first_wall:
            category = 'offered-before-first-submission'
        else:
            category = 'interior-unsubmitted'
        missing.append(dict(originalIndex=a['originalIndex'], category=category))
    return dict(missing=missing, causeInferred=False,
                note='Ordering categories are not drop causes; consult queue counters and exact events. EOF tails are never filled with synthetic frames.')


def execute(plan, host, test_dll, plugin_root, variant, output, visual=False):
    host, test_dll, plugin_root = host.resolve(strict=True), test_dll.resolve(strict=True), plugin_root.resolve(strict=True)
    plugin = plugin_root / ('vprenderer-beta/VideoProcessorVPRendererBeta.dll' if variant == 'beta' else 'vprenderer/VideoProcessorVPRenderer.dll')
    producers = [host, test_dll, *sorted(plugin.parent.glob('*.dll'))]
    if not plugin.is_file():
        raise ValueError('Renderer plugin absent')
    before = {str(p): digest(p) for p in producers}
    (output/'producer.json').write_text(json.dumps({'producerFiles': before, 'config': plan['config'], 'configSha256': digest(Path(plan['config']))}, indent=2)+'\n')
    initial_idle = require_closed_and_idle()
    runs = []
    deferred = None
    for segment in plan['segments']:
        try:
            idle = require_closed_and_idle()
        except (ValueError, subprocess.SubprocessError) as error:
            deferred = {'segment': segment['number'], 'reason': str(error), 'started': False}
            break
        directory = Path(segment['directory'])
        local_host = directory / 'native-renderer-host.exe'
        shutil.copy2(host, local_host)
        if digest(local_host) != before[str(host)]:
            raise ValueError('Isolated host copy hash mismatch')
        env = os.environ.copy()
        env.update(VP_GENERIC_CAPTURE_ROOT=plan['captureRoot'], VP_GENERIC_CAPTURE_MANIFEST=segment['inputs'],
                   VP_GENERIC_CAPTURE_PINS=segment['pins'], VP_GENERIC_CAPTURE_OUTPUT=str(directory),
                   VP_GENERIC_CAPTURE_PRELOAD_MIB=str(plan.get('preloadMiB',0)), VP_GENERIC_CAPTURE_CLOCK_HZ=str(plan['clockHz']), VP_GENERIC_CAPTURE_PROFILE=plan['profile'],
                   VP_GENERIC_CAPTURE_CONFIG=plan['config'], VP_RENDERER_REGRESSION_PLUGIN_ROOT=str(plugin_root),
                   VP_CADENCE_CACHE_DIAGNOSTICS='1', VP_GENERIC_CAPTURE_VISUAL='1' if visual else '0')
        with (directory / 'stdout.txt').open('w') as stdout, (directory / 'stderr.txt').open('w') as stderr:
            try:
                process = subprocess.run([str(local_host), str(test_dll), '21' if variant == 'beta' else '20'], env=env,
                                         stdout=stdout, stderr=stderr, timeout=210, creationflags=subprocess.CREATE_NO_WINDOW)
                code = process.returncode
            except subprocess.TimeoutExpired:
                code = None
        summary_path = directory / 'summary.json'
        summary = json.loads(summary_path.read_text()) if summary_path.exists() else None
        records_path = directory / 'records.jsonl'
        records = [json.loads(line) for line in records_path.read_text().splitlines()] if records_path.exists() else []
        inputs_path = directory / 'inputs.jsonl'
        offered = {r['index']: r for r in (json.loads(line) for line in inputs_path.read_text().splitlines())} if inputs_path.exists() else {}
        annotations = []
        for frame in segment['frames']:
            identity = frame['identity_v1']
            matching = [r for r in records if r['source'] == identity[2] and r['timestamp'] == identity[3]]
            annotations.append(dict(originalIndex=frame['index'], originalIdentity=identity, input=offered.get(frame['index']),
                                    submissions=[r for r in matching if r['stage']=='submitted-presentation' and r['flags']&1],
                                    measurements=[r for r in matching if r['stage'] in ('source-picture-active','source-picture-job','subtitle-output','subtitle-canvas-admission')]))
        (directory/'frames.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in annotations))
        visuals = [{'path': str(p), 'sha256': digest(p)} for p in sorted((directory/'screenshots').rglob('*')) if p.is_file()] if (directory/'screenshots').exists() else []
        exact_inputs = len(offered)==len(segment['frames']) and all(
            offered.get(f['index'], {}).get('originalIdentity') == f['identity_v1'] for f in segment['frames'])
        submitted_records = [r for r in records if r['stage']=='submitted-presentation' and r['flags']&1]
        original_pairs = {(f['identity_v1'][2], f['identity_v1'][3]) for f in segment['frames']}
        submitted_pairs = {(r['source'], r['timestamp']) for r in submitted_records}
        output_mapping = bool(summary and len(submitted_records)==summary.get('submittedEvents')
                              and len(submitted_pairs)==summary.get('distinctSubmittedInputs')
                              and submitted_pairs <= original_pairs)
        some_coverage = bool(code==0 and summary and output_mapping and summary.get('inputs')==len(segment['frames'])
                             and summary.get('submittedEvents',0)>0 and summary.get('foreignSubmitted')==0
                             and summary.get('diagnosticRecordsLost')==0 and exact_inputs)
        runs.append(dict(number=segment['number'], exitCode=code, initialIdle=idle,
                         exactOriginalInputsOffered=exact_inputs, outputMappingVerified=output_mapping, integrityAndSomeRendererCoverage=some_coverage,
                         omissions=describe_omissions(annotations), summary=summary, summarySha256=digest(summary_path) if summary else None, visuals=visuals))
        (output/'progress.json').write_text(json.dumps({'producerFiles': before, 'runs': runs}, indent=2)+'\n')
        if code is None:
            break
    stable = before == {str(p): digest(p) for p in producers}
    receipt = dict(schema=1, producerFiles=before, producerFilesStable=stable, runs=runs, initialIdle=initial_idle,
                   completed=len(runs)==len(plan['segments']), deferred=deferred, integrityAndSomeRendererCoverage=stable and len(runs)==len(plan['segments']) and all(r['integrityAndSomeRendererCoverage'] for r in runs),
                   inputScheduleQualified=len(runs)==len(plan['segments']) and all(r['summary'] and r['summary'].get('inputScheduleQualified',False) for r in runs),
                   allOriginalInputsOffered=len(runs)==len(plan['segments']) and all(r['exactOriginalInputsOffered'] for r in runs),
                   allOriginalInputsObservedSubmitted=len(runs)==len(plan['segments']) and all(r['summary'] and r['summary'].get('distinctSubmittedInputs')==r['summary'].get('inputs') for r in runs),
                   visualReadbacksRequested=visual, timingRunUninstrumented=False, completeStreamClaim=False, screenVisiblePresentationClaim=False, captionPixelPreservationProven=False,
                   expectedAspectSupplied=False, historicalTransportIdentitiesReplayed=False,
                   externalWorkloadExclusionGuaranteed=False,
                   limitations=['Fresh runtime transport/accepted IDs are mapped to original physical counters and exact ticks.',
                                'Gap boundaries start cold independent renderers; no fake frames bridge them.',
                                'No warmup input repetition. Cold shader/queue effects and unsubmitted tail inputs remain visible.',
                                'Input file IO/hash and diagnostic collection affect measured cost. GPU idle was observed before each run, not guaranteed against other processes starting later.',
                                'Submitted events are not proof of physical display visibility or caption/picture pixel correctness.'])
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--capture-clock-hz', type=int, required=True)
    parser.add_argument('--profile', choices=PROFILES, required=True)
    parser.add_argument('--screen-aspect', type=float, default=2.35)
    parser.add_argument('--preload-mib', type=int, default=0, help='Opt-in bounded native preload before timed input; zero streams IO/hash on producer')
    parser.add_argument('--host', type=Path)
    parser.add_argument('--test-dll', type=Path)
    parser.add_argument('--plugin-root', type=Path)
    parser.add_argument('--variant', choices=('standard','beta'), default='standard')
    parser.add_argument('--visual', action='store_true', help='Request two source-linked readbacks per segment; timing includes capture cost')
    parser.add_argument('--prepare-only', action='store_true', help='Validate inputs and write the plan without GPU or process activity')
    args = parser.parse_args()
    try:
        plan = prepare(args.manifest, args.output, args.capture_clock_hz, args.profile, args.screen_aspect, args.preload_mib)
        if args.prepare_only:
            print(json.dumps({'prepared': True, 'segments': len(plan['segments']), 'gpu': False}))
            return 0
        if not all((args.host, args.test_dll, args.plugin_root)):
            raise ValueError('Execution requires --host, --test-dll and --plugin-root')
        receipt = execute(plan, args.host, args.test_dll, args.plugin_root, args.variant, args.output.resolve(), args.visual)
        print(json.dumps({'integrityAndSomeRendererCoverage': receipt['integrityAndSomeRendererCoverage'], 'receipt': str(args.output/'receipt.json')}))
        return 0 if receipt['integrityAndSomeRendererCoverage'] else 1
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
