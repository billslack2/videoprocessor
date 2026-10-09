"""Observe reviewed rendered subtitle recordings without changing input pixels."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import cv2
import numpy as np


def neutral_ink(frame):
    pixels = frame.astype(np.int16)
    return ((pixels.min(2) > 170) & (pixels.max(2) - pixels.min(2) < 50)).astype(np.float32)


def crop(frame, rect):
    left, top, right, bottom = rect
    return frame[top:bottom, left:right]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--media-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).with_name('subtitle_recorded_output_manifest.json'))
    parser.add_argument('--report-only', action='store_true')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError('Use a fresh output directory')
    args.output.mkdir(parents=True, exist_ok=True)
    cv2.setNumThreads(1)
    report = {'kind': 'rendered-output-observer', 'clean_detector_acceptance': False,
              'note': manifest['note'], 'cases': [], 'failures': [],
              'manifest_sha256': hashlib.sha256(args.manifest.read_bytes()).hexdigest()}
    for clip in manifest['clips']:
        path = args.media_root / clip['basename']
        if hashlib.sha256(path.read_bytes()).hexdigest() != clip['sha256']:
            raise ValueError('Recording hash differs: ' + str(path))
        cap = cv2.VideoCapture(str(path))
        if (int(cap.get(3)), int(cap.get(4))) != (manifest['width'], manifest['height']) or abs(cap.get(5)-manifest['fps']) > .001:
            raise ValueError('Unexpected recording geometry or cadence')
        for case in clip['cases']:
            template = None
            if case['kind'] == 'glyph_position':
                cap.set(cv2.CAP_PROP_POS_FRAMES, case['reference_frame'])
                ok, frame = cap.read()
                if not ok: raise ValueError('Cannot read template frame')
                template = neutral_ink(crop(frame, case['template_rect']))
            first, end = case['frames']
            cap.set(cv2.CAP_PROP_POS_FRAMES, first)
            rows = []
            for number in range(first, end):
                ok, frame = cap.read()
                if not ok: raise ValueError('Cannot read capture frame')
                left, top, _, _ = case['search_rect']
                pixels = crop(frame, case['search_rect'])
                row = {'capture_frame': number, 'seconds': number/manifest['fps']}
                if template is None:
                    b, g, r = cv2.split(pixels.astype(np.int16))
                    ys, xs = np.where((g > 150) & (g > b+60) & (g > r+60))
                    row['green_bbox'] = None if len(xs) < 30 else [int(xs.min()+left), int(ys.min()+top), int(xs.max()+left+1), int(ys.max()+top+1)]
                    row['state'] = 'present' if row['green_bbox'] else 'missing'
                else:
                    scores = cv2.matchTemplate(neutral_ink(pixels), template, cv2.TM_CCORR_NORMED)
                    _, score, _, point = cv2.minMaxLoc(scores)
                    row.update(x=point[0]+left, y=point[1]+top, match=score)
                    expected = case['expected_template_xy'][1]
                    row['state'] = ('unmatched' if score < case['minimum_match'] else 'moved' if abs(row['y']-expected) <= 1 else 'source' if abs(row['y']-expected-case['source_delta_y']) <= 1 else 'shifted')
                rows.append(row)
            with (args.output/(case['id']+'.csv')).open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
            runs = []
            for row in rows:
                if not runs or row['state'] != runs[-1]['state']:
                    runs.append({'state': row['state'], 'first_frame': row['capture_frame'], 'end_frame': row['capture_frame']+1})
                else: runs[-1]['end_frame'] = row['capture_frame']+1
            states = {state: sum(row['state']==state for row in rows) for state in sorted(set(row['state'] for row in rows))}
            summary = {'id': case['id'], 'clip': clip['basename'], 'frames': case['frames'], 'states': states, 'runs': runs}
            if template is None:
                boxes = np.array([row['green_bbox'] for row in rows if row['green_bbox']])
                summary['bbox_coordinate_spread'] = (boxes.max(0)-boxes.min(0)).tolist() if len(boxes) else []
                failed = states.get('missing',0)>0 or (len(boxes)>0 and max(summary['bbox_coordinate_spread'])>2)
            else: failed = any(state!='moved' for state in states)
            summary['historical_failure_observed'] = bool(failed)
            report['cases'].append(summary)
            if failed: report['failures'].append(case['id'])
        cap.release()
    (args.output/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return 0 if args.report_only or not report['failures'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
