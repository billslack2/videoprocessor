"""Measure existing rendered recordings, preserving all input pixels and overlays.

This is an output-behavior regression, not clean detector-input acceptance.
Optional probe replay is diagnostic only: green borders and prior cut/paste
operations are deliberately retained. No OCR, erasure, or inverse movement.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

import cv2
import numpy as np


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024*1024), b""):
            h.update(block)
    return h.hexdigest()


def neutral_ink(pixels):
    low, high = pixels.min(2), pixels.max(2)
    return ((low > 170) & (high.astype(np.int16)-low < 45)).astype(np.float32)


def green_bounds(pixels):
    # Search only the manually reviewed caption neighborhood. Cast before
    # arithmetic so bright channels cannot wrap at uint8 boundaries.
    region = pixels[850:1040, 400:1600].astype(np.int16)
    b, g, r = region[:, :, 0], region[:, :, 1], region[:, :, 2]
    ys, xs = np.where((g > 150) & (g > b+50) & (g > r+50))
    if len(xs) <= 20:
        return None, len(xs)
    return [int(xs.min()+400), int(ys.min()+850), int(xs.max()+401), int(ys.max()+851)], len(xs)


def glyph_positions(pixels, template, rectangle):
    left, top, right, bottom = rectangle
    search_top = top-100
    search = neutral_ink(pixels[search_top:bottom+3, left-3:right+3])
    scores = cv2.matchTemplate(search, template, cv2.TM_CCORR_NORMED)
    source = scores[top-search_top-3:top-search_top+4]
    moved = scores[:top-search_top-10]
    _, source_score, _, source_point = cv2.minMaxLoc(source)
    _, moved_score, _, moved_point = cv2.minMaxLoc(moved)
    at_source, at_moved = source_score >= .8, moved_score >= .8
    state = "duplicate" if at_source and at_moved else "source" if at_source else "moved" if at_moved else "missing"
    y = top-3+source_point[1] if source_score >= moved_score else search_top+moved_point[1]
    return state, int(y), float(source_score), float(moved_score)


def contains(outer, inner):
    return outer and outer[0] <= inner[0] and outer[1] <= inner[1] and outer[2] >= inner[2] and outer[3] >= inner[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--media-root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("subtitle_rendered_loops_manifest.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--probe", type=Path, help="Optional diagnostic replay; contaminated input never qualifies clean detector acceptance")
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--ffmpeg", type=Path)
    parser.add_argument("--report-only", action="store_true", help="Save failing historical baseline without a nonzero exit")
    args = parser.parse_args()
    cv2.setNumThreads(1)
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError("Use a new output directory to preserve previous results")
    if args.render and not args.ffmpeg:
        raise ValueError("--render requires an existing --ffmpeg executable")
    manifest = json.loads(args.manifest.read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"kind": "rendered-output-observer", "clean_detector_acceptance": False,
              "manifest_sha256": digest(args.manifest), "clips": {}, "failures": [],
              "note": "Original rendered pixels and existing overlays retained. Template matching identifies manually selected glyph shapes, not recognized text. 60Hz capture may repeat source frames."}
    pinned_probe = None
    if args.probe:
        pinned_probe = args.output / "SubtitleClipProbe.exe"
        shutil.copy2(args.probe, pinned_probe)
        report["diagnostic_probe_sha256"] = digest(pinned_probe)
    x, y, width, height = manifest["crop_xywh"]
    for clip in manifest["clips"]:
        path = args.media_root / clip["basename"]
        if digest(path) != clip["sha256"]:
            raise ValueError("Media differs from frozen reviewed recording: " + str(path))
        cap = cv2.VideoCapture(str(path))
        if int(cap.get(3)) != 2560 or int(cap.get(4)) != 1440 or abs(cap.get(5)-manifest["fps"]) > .001:
            raise ValueError("Recording dimensions/cadence differ")
        templates = {}
        for label in clip["labels"]:
            cap.set(cv2.CAP_PROP_POS_FRAMES, label["reference_frame"])
            ok, frame = cap.read()
            if not ok: raise ValueError("Cannot read template reference")
            l, t, r, b = label["template_box"]
            templates[label["id"]] = neutral_ink(frame[y+t:y+b, x+l:x+r])
        first, last = clip["capture_frames"]
        cap.set(cv2.CAP_PROP_POS_FRAMES, first)
        probe = encoder = None
        streams = []
        rows = []
        if pinned_probe:
            probe_out = (args.output / (clip["id"]+"-diagnostic-probe.csv")).open("w", newline="")
            probe_log = (args.output / (clip["id"]+"-diagnostic-probe.log")).open("w")
            streams += [probe_out, probe_log]
            probe = subprocess.Popen([str(pinned_probe), str(width), str(height), "3"], stdin=subprocess.PIPE, stdout=probe_out, stderr=probe_log)
        if args.render:
            encoder = subprocess.Popen([str(args.ffmpeg), "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "bgr24",
                "-s", f"{width}x{height+65}", "-r", str(manifest["fps"]), "-i", "-", "-an", "-c:v", "libx264", "-preset", "fast", "-crf", "18",
                "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(args.output/(clip["id"]+"-rendered-crop.mp4"))], stdin=subprocess.PIPE)
        try:
            for number in range(first, last):
                ok, frame = cap.read()
                if not ok: raise ValueError("Cannot decode original frame " + str(number))
                pixels = frame[y:y+height, x:x+width]
                bgra = cv2.cvtColor(pixels, cv2.COLOR_BGR2BGRA).tobytes()
                if probe: probe.stdin.write(bgra)
                label = next((item for item in clip["labels"] if item["frames"][0] <= number < item["frames"][1]), None)
                green, count = green_bounds(pixels)
                row = {"capture_frame": number, "seconds": number/manifest["fps"], "label": label["id"] if label else "blank",
                       "recorded_mode": clip["rendered_mode"], "green_pixels": count, "green_bbox": json.dumps(green),
                       "glyph_state": "unlabelled", "glyph_top": -1, "source_score": 0.0, "moved_score": 0.0,
                       "green_state": "none" if not green else "unlabelled", "crop_bgra_sha256": hashlib.sha256(bgra).hexdigest()}
                if label:
                    state, top, source_score, moved_score = glyph_positions(pixels, templates[label["id"]], label["template_box"])
                    row.update(glyph_state=state, glyph_top=top, source_score=source_score, moved_score=moved_score)
                    row["green_state"] = "none" if not green else "full" if all(contains(green, line) for line in label["line_boxes"]) else "partial"
                rows.append(row)
                if encoder:
                    canvas = np.zeros((height+65, width, 3), np.uint8)
                    canvas[64:64+height] = pixels
                    cv2.putText(canvas, f'{number/60:.3f}s frame={number} {row["label"]} green={row["green_state"]} glyph={row["glyph_state"]}', (12, 26), cv2.FONT_HERSHEY_SIMPLEX, .7, (255,255,255), 1, cv2.LINE_AA)
                    cv2.putText(canvas, 'EXISTING RENDERED OUTPUT / overlays retained / diagnostic input only', (12, 52), cv2.FONT_HERSHEY_SIMPLEX, .65, (255,255,255), 1, cv2.LINE_AA)
                    encoder.stdin.write(canvas.tobytes())
        finally:
            cap.release()
            for process in (probe, encoder):
                if process:
                    process.stdin.close()
                    if process.wait(): raise RuntimeError("Probe/encoder failed")
            for stream in streams: stream.close()
        with (args.output/(clip["id"]+"-observer.csv")).open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=rows[0])
            writer.writeheader(); writer.writerows(rows)
        cues = {}
        for label in clip["labels"]:
            selected = [row for row in rows if row["label"] == label["id"]]
            failures = []
            states = [row["glyph_state"] for row in selected]
            positions = [row["glyph_top"] for row in selected if row["glyph_state"] not in ("missing", "duplicate")]
            boxes = [row["green_bbox"] for row in selected if row["green_state"] != "none"]
            if not selected: failures.append("annotation_not_sampled")
            if any(state in ("missing", "duplicate") for state in states): failures.append("missing_or_duplicated_glyphs")
            if clip["rendered_mode"] == "bbox":
                if any(row["green_state"] != "full" for row in selected): failures.append("green_bbox_missing_or_partial")
                if len(set(boxes)) > 1: failures.append("green_bbox_drift")
            else:
                if len(set(states)) != 1: failures.append("source_destination_alternation")
                if len(set(positions)) > 1: failures.append("glyph_position_drift")
            cues[label["id"]] = {"frames": len(selected), "green_states": {state: sum(row["green_state"] == state for row in selected) for state in ("full", "partial", "none")},
                "glyph_states": {state: states.count(state) for state in ("source", "moved", "duplicate", "missing")},
                "distinct_green_boxes": len(set(boxes)), "glyph_top_values": sorted(set(positions)),
                "state_transitions": [{"capture_frame": row["capture_frame"], "state": row["glyph_state"]} for i, row in enumerate(selected) if i == 0 or row["glyph_state"] != selected[i-1]["glyph_state"]],
                "failures": failures}
            if failures: report["failures"].append({"cue": label["id"], "failures": failures})
        report["clips"][clip["id"]] = {"frames": len(rows), "source_sha256": clip["sha256"], "recorded_mode": clip["rendered_mode"], "cues": cues}
        print(clip["id"], json.dumps(cues), flush=True)
    report["output_behavior_passed"] = not report["failures"]
    (args.output/"report.json").write_text(json.dumps(report, indent=2))
    return 0 if args.report_only or report["output_behavior_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
