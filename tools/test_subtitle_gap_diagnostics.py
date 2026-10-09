"""Check short, unchanged no-box recording slices as diagnostic regressions only.

Each slice starts fresh and is selected from an observed failure. Passing this
check does not establish continuous raw-input or GPU-renderer acceptance.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def check_cue(cue, rows, mapped):
    selected = [(row, frame) for row, frame in zip(rows, mapped)
                if frame['annotation'] == cue['id']]
    failures = []
    if not selected:
        return {'id': cue['id'], 'sampled_frames': 0, 'failures': ['no_samples']}
    boxes, identities = [], []
    missing, incomplete, wrong_lines = [], [], []
    for row, frame in selected:
        number = frame['capture_frame']
        displayed = bool(int(row['displayed']))
        box = tuple(int(row[edge]) for edge in ('left', 'top', 'right', 'bottom'))
        boxes.append(box if displayed else None)
        identities.append(int(row['cue']) if displayed else 0)
        if not displayed:
            missing.append(number)
        elif not all(box[0] <= line[0] and box[1] <= line[1] and
                     box[2] >= line[2] and box[3] >= line[3]
                     for line in cue['line_boxes']):
            incomplete.append(number)
        if displayed and int(row['lines']) != len(cue['line_boxes']):
            wrong_lines.append(number)
    if missing: failures.append('missing_display')
    if incomplete: failures.append('incomplete_bounds')
    if wrong_lines: failures.append('wrong_line_count')
    if len(set(boxes)) != 1: failures.append('displayed_box_changed')
    if 0 in identities or len(set(identities)) != 1: failures.append('cue_identity_changed_or_zero')
    return {'id': cue['id'], 'sampled_frames': len(selected),
            'capture_frames': [frame['capture_frame'] for _, frame in selected],
            'displayed_boxes': boxes, 'cue_ids': identities,
            'missing_capture_frames': missing, 'incomplete_capture_frames': incomplete,
            'wrong_line_count_capture_frames': wrong_lines, 'failures': failures,
            'temporal_evidence_available': len(selected) > 1}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    manifest_path = directory / 'subtitle_no_box_diagnostic_manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if not manifest['annotations'] or any(cue['split'] != 'diagnostic' or not cue['line_boxes']
                                          for cue in manifest['annotations']):
        raise ValueError('This wrapper requires positive diagnostic-only annotations')
    media_hash = digest(args.video)
    if media_hash != manifest['media']['sha256']:
        raise ValueError('Original recording SHA differs from frozen reviewed annotations')
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError('Use a fresh output directory to preserve previous evidence')
    args.output.mkdir(parents=True, exist_ok=True)
    pinned_probe = args.output / 'SubtitleClipProbe.exe'
    shutil.copy2(args.probe, pinned_probe)
    probe_hash = digest(pinned_probe)
    report = {'kind': 'short-slice-diagnostic-regression', 'clean_detector_acceptance': False,
              'gpu_renderer_acceptance': False, 'diagnostic_regression_passed': False,
              'media_sha256': media_hash, 'manifest_sha256': digest(manifest_path),
              'probe_sha256': probe_hash, 'modes': {},
              'limitations': manifest['limitations'],
              'note': 'Original decoded crops are unmodified. Independent short slices cannot prove continuous cue tracking, onset handling or live GPU output. One-sample slices cannot establish temporal consistency.'}
    for lookahead in (3, 0):
        run_directory = args.output / ('lookahead-' + str(lookahead))
        subprocess.run([sys.executable, str(directory / 'test_subtitle_recording.py'),
                        '--video', str(args.video.resolve()), '--probe', str(pinned_probe.resolve()),
                        '--manifest', str(manifest_path), '--output', str(run_directory.resolve()),
                        '--sample-fps', '24000/1001', '--lookahead', str(lookahead), '--report-only'], check=True)
        replay = json.loads((run_directory / 'report.json').read_text(encoding='utf-8'))
        if replay['probe_sha256'] != probe_hash or replay['media_sha256'] != media_hash or replay['accepted']:
            raise ValueError('Unexpected probe/media identity or acceptance classification')
        cases = []
        for segment in manifest['segments']:
            with (run_directory / (segment['id'] + '.csv')).open(newline='') as stream:
                rows = list(csv.DictReader(stream))
            mapped = json.loads((run_directory / (segment['id'] + '-input.json')).read_text())
            if len(rows) != len(mapped): raise ValueError('Input/output frame count differs')
            for cue in manifest['annotations']:
                if cue['segment'] == segment['id']:
                    cases.append(check_cue(cue, rows, mapped))
        report['modes'][str(lookahead)] = {'diagnostic_regression_passed': all(not case['failures'] for case in cases), 'cases': cases}
    report['diagnostic_regression_passed'] = all(mode['diagnostic_regression_passed'] for mode in report['modes'].values())
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'diagnostic_regression_passed': report['diagnostic_regression_passed'],
                      'report': str(args.output / 'report.json')}, indent=2))
    return 0 if report['diagnostic_regression_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
