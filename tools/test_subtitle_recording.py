"""Replay a privately supplied recording without changing its pixels.

The checked-in manifest contains manual annotations, not media or detector-derived
ground truth. OpenCV is used only for video decoding, cropping and BGRA packing.
No resizing, OCR, overlay removal, thresholding or image reconstruction occurs.
Build tools/subtitle_clip_probe.vcxproj before running this script.
"""
import argparse
import csv
import hashlib
import json
import shutil
import subprocess
import sys
from collections import defaultdict
from fractions import Fraction
from pathlib import Path

import cv2
import numpy as np


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def contains(box, expected, tolerance=0):
    return (box[0] <= expected[0] + tolerance and box[1] <= expected[1] + tolerance
            and box[2] >= expected[2] - tolerance and box[3] >= expected[3] - tolerance)


def csv_rect(row, prefix):
    return tuple(int(row[prefix + edge]) for edge in ("left", "top", "right", "bottom"))


def cut_paste_checks(row, width, height):
    """Check presentation geometry separately; raw detection gates never use padding."""
    if not int(row["displayed"]):
        return [] if not int(row["cut_valid"]) else ["cut_without_detection"]
    raw = csv_rect(row, "")
    padded = csv_rect(row, "padded_")
    expected = (max(0, raw[0]-30), max(0, raw[1]-30),
                min(width, raw[2]+30), min(height, raw[3]+10))
    failures = []
    if padded != expected: failures.append("incorrect_display_padding")
    if not int(row["cut_valid"]):
        return failures + ["cut_placement_unavailable"]
    source, target = csv_rect(row, "cut_source_"), csv_rect(row, "cut_destination_")
    if source != padded: failures.append("incorrect_cut_source")
    if source[0] != target[0] or source[2] != target[2] or source[3]-source[1] != target[3]-target[1]:
        failures.append("cut_changes_size_or_horizontal_position")
    if target[1] >= source[1]: failures.append("cut_not_shifted_upward")
    if target[3] != int(row["cut_picture_bottom"])-15: failures.append("incorrect_cut_bottom_inset")
    if target[1] < int(row["cut_picture_top"]) or target[3] > int(row["cut_picture_bottom"]):
        failures.append("cut_outside_active_picture")
    return failures


def apply_cut_paste_preview(original, row):
    """CPU preview only; copy from immutable current pixels, destination wins overlap."""
    rendered = original.copy()
    if int(row["cut_valid"]):
        sl, st, sr, sb = csv_rect(row, "cut_source_")
        dl, dt, dr, db = csv_rect(row, "cut_destination_")
        patch = original[st:sb, sl:sr].copy()
        rendered[st:sb, sl:sr] = 0
        rendered[dt:db, dl:dr] = patch
    return rendered


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--video", type=Path, required=True)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--manifest", type=Path,
                        default=Path(__file__).with_name("subtitle_phantom_manifest.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--segment", action="append", help="Default: all annotated crop phases")
    parser.add_argument("--lookahead", type=int, default=3)
    parser.add_argument("--sample-fps", default="24000/1001",
                        help="Screen capture duplicates are not independent source observations")
    parser.add_argument("--diagnostic-bars", action="store_true",
                        help="Override bar extraction ONLY for explicitly labelled diagnostics")
    parser.add_argument("--start-seconds", type=float, help="Optional replay slice, recording time")
    parser.add_argument("--end-seconds", type=float, help="Optional replay slice, recording time")
    parser.add_argument("--report-only", action="store_true", help="Record failures without passing regression acceptance")
    parser.add_argument("--render", action="store_true", help="Write native-size annotated MP4 previews after replay")
    parser.add_argument("--render-only", action="store_true", help="Render existing CSV/input maps without rerunning or changing scores")
    parser.add_argument("--render-mode", choices=("bbox", "cut-paste"), default="bbox",
                        help="Preview padded green box or rectangular copy/clear using production placement")
    parser.add_argument("--ffmpeg", type=Path, help="Optional existing ffmpeg executable for H.264 previews")
    parser.add_argument("--inject-measurement-gap", type=int, nargs=2, action="append", default=[],
                        metavar=("CAPTURE_FRAME", "SAMPLES"),
                        help="Explicit fault test: clear segmentation for N sampled observations starting at/after capture frame; preserve original pixels/ink")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    known_segments = {segment["id"] for segment in manifest["segments"]}
    if args.segment and set(args.segment) - known_segments:
        raise ValueError("Unknown segment: " + ", ".join(sorted(set(args.segment) - known_segments)))
    if args.diagnostic_bars and not args.report_only:
        raise ValueError("Supplied bar boundaries are diagnostic-only; use --report-only")
    media_hash = sha256_file(args.video)
    if media_hash != manifest["media"]["sha256"]:
        raise ValueError("Recording hash differs from manually reviewed source")
    cap = cv2.VideoCapture(str(args.video))
    fps = cap.get(cv2.CAP_PROP_FPS)
    actual = [int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)), int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT)),
              int(cap.get(cv2.CAP_PROP_FRAME_COUNT))]
    if actual != manifest["media"]["width_height_frames"] or abs(fps - manifest["media"]["fps"]) > .001:
        raise ValueError("Decoded recording metadata differs from annotation")
    sample_fps = float(Fraction(args.sample_fps))
    if not 0 < sample_fps <= fps or not 0 <= args.lookahead <= 7:
        raise ValueError("Invalid sampling rate or lookahead")
    if any(first < 0 or count <= 0 for first, count in args.inject_measurement_gap):
        raise ValueError("Invalid segmentation fault range")
    if not args.render_only and args.output.exists() and any(args.output.iterdir()):
        raise ValueError("Use a new output directory; prior replay artifacts are immutable")
    args.output.mkdir(parents=True, exist_ok=True)
    if args.render_only:
        previous = json.loads((args.output / "report.json").read_text(encoding="utf-8"))
        if previous["media_sha256"] != media_hash or previous["manifest_sha256"] != sha256_file(args.manifest):
            raise ValueError("Existing replay source or annotation hash differs")
        rendered = 0
        for segment in manifest["segments"]:
            name = segment["id"]
            if name not in previous["segments"] or (args.segment and name not in args.segment): continue
            mapped = json.loads((args.output / (name + "-input.json")).read_text(encoding="utf-8"))
            with (args.output / (name + ".csv")).open(newline="") as source:
                rows = list(csv.DictReader(source))
            if len(mapped) != len(rows): raise ValueError("Existing frame map/output lengths differ")
            labels = [cue for cue in manifest["annotations"] if cue["segment"] == name]
            render_preview(cap, mapped, rows, segment, labels, float(Fraction(previous["sample_fps"])),
                           args.output / (name + "-" + args.render_mode + "-preview.mp4"), args.ffmpeg, args.render_mode)
            rendered += 1
        cap.release()
        if not rendered: raise ValueError("No existing segment results were selected")
        return 0
    if args.probe is None:
        raise ValueError("--probe is required for numerical replay")
    probe_hash = sha256_file(args.probe)
    pinned_probe = args.output / "SubtitleClipProbe.exe"
    shutil.copy2(args.probe, pinned_probe)
    if sha256_file(pinned_probe) != probe_hash:
        raise ValueError("Probe changed while copying its per-run snapshot")
    report = {"media_sha256": media_hash, "manifest_sha256": sha256_file(args.manifest),
              "probe_sha256": probe_hash, "opencv": cv2.__version__,
              "sample_fps": args.sample_fps, "lookahead": args.lookahead,
              "presentation_padding_pixels": {"sides": 30, "top": 30, "bottom": 10}, "cut_bottom_gap_pixels": 15,
              "injected_measurement_gaps_capture_frame_samples": args.inject_measurement_gap,
              "diagnostic_supplied_bars": args.diagnostic_bars,
              "note": "Native recorded raster; this is not a recovery of the original 4K input. "
                      "Sampling is deterministic and does not establish source-frame identity. "
                      "Each crop phase starts a fresh process; resize transition state is not replayed. "
                      "Strict containment gates acceptance; 2px tolerance is sensitivity reporting only. "
                      "Raw detection metrics exclude the additional 30px top/side and 10px bottom presentation padding. "
                      "Cut/paste previews apply production placement to original pixels in software; "
                      "they do not validate the GPU shader. "
                      "The manifest's heldout label denotes independently annotated reserved cues; "
                      "these cues have now been exercised during iteration, not blind final validation.",
              "report_only": args.report_only, "segments": {}}
    acceptance_failures = []
    used_injections = set()
    for segment in manifest["segments"]:
        name = segment["id"]
        if args.segment and name not in args.segment:
            continue
        start, end = segment["frames"]
        if args.start_seconds is not None:
            start = max(start, round(args.start_seconds * fps))
        if args.end_seconds is not None:
            end = min(end, round(args.end_seconds * fps))
        if start >= end:
            continue
        crop_x, crop_y, width, height = segment["crop_xywh"]
        command = [str(pinned_probe), str(width), str(height), str(args.lookahead)]
        if args.diagnostic_bars:
            command += ["--diagnostic-bars", *map(str, segment["reviewed_picture_y"])]
        target_frames = []
        sample_index = 0
        while True:
            frame = start + round(sample_index * fps / sample_fps)
            if frame >= end:
                break
            target_frames.append(frame)
            sample_index += 1
        for injection_index, (first, count) in enumerate(args.inject_measurement_gap):
            if start <= first < end:
                first_sample = next((i for i, frame in enumerate(target_frames) if frame >= first), None)
                if first_sample is None or first_sample+count > len(target_frames):
                    raise ValueError("Segmentation fault extends beyond selected replay segment")
                command += ["--inject-measurement-gap", str(first_sample), str(count)]
                used_injections.add(injection_index)
        output_csv = args.output / (name + ".csv")
        labels = [cue for cue in manifest["annotations"] if cue["segment"] == name]
        mapped = []
        with output_csv.open("w", newline="") as csv_out, (args.output / (name + ".log")).open("w") as log:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=csv_out, stderr=log)
            try:
                cap.set(cv2.CAP_PROP_POS_FRAMES, start)
                index = start
                for frame in target_frames:
                    while index <= frame:
                        ok, pixels = cap.read()
                        if not ok:
                            raise RuntimeError(f"Cannot decode original frame {index}")
                        index += 1
                    crop = pixels[crop_y:crop_y + height, crop_x:crop_x + width]
                    if crop.shape[:2] != (height, width):
                        raise ValueError("Movie crop lies outside decoded source raster")
                    bgra = cv2.cvtColor(crop, cv2.COLOR_BGR2BGRA).tobytes()
                    matches = [cue["id"] for cue in labels if cue["frames"][0] <= frame < cue["frames"][1]]
                    if len(matches) > 1:
                        raise ValueError(f"Overlapping annotations at frame {frame}")
                    mapped.append({"input_frame": len(mapped), "capture_frame": frame,
                                   "capture_seconds": frame / fps, "annotation": matches[0] if matches else None,
                                   "bgra_sha256": hashlib.sha256(bgra).hexdigest()})
                    process.stdin.write(bgra)
            finally:
                process.stdin.close()
                return_code = process.wait()
            if return_code:
                raise RuntimeError(f"Probe exited {return_code}; see {name}.log")
        (args.output / (name + "-input.json")).write_text(json.dumps(mapped, indent=2), encoding="utf-8")
        with output_csv.open(newline="") as source:
            rows = list(csv.DictReader(source))
        if len(rows) != len(mapped):
            raise RuntimeError("Probe did not return exactly one result per input frame")
        per_cue = defaultdict(list)
        for row, source_frame in zip(rows, mapped):
            if int(row["input_frame"]) != source_frame["input_frame"]:
                raise RuntimeError("Probe frame mapping is not sequential")
            if source_frame["annotation"]:
                per_cue[source_frame["annotation"]].append((row, source_frame))
        results = {}
        for cue in labels:
            items = per_cue[cue["id"]]
            if not items:
                if cue["split"] != "diagnostic" and cue["frames"][0] < end and cue["frames"][1] > start:
                    acceptance_failures.append({"cue": cue["id"], "failures": ["annotation_not_sampled"]})
                continue
            expected = cue["line_boxes"]
            positive = bool(expected)
            boxes, complete, strict, presentation_complete, areas, edge_padding = [], [], [], [], [], []
            placement_failures, placements, current_gaps = [], [], []
            for row, source_frame in items:
                displayed = bool(int(row["displayed"]))
                box = tuple(int(row[field]) for field in ("left", "top", "right", "bottom")) if displayed else None
                boxes.append(box)
                placement_failures.extend(cut_paste_checks(row, width, height))
                if int(row["cut_valid"]):
                    placements.append(csv_rect(row, "cut_destination_"))
                    current_gaps.append(int(row["picture_bottom"])-int(row["cut_destination_bottom"]))
                strict.append(displayed and all(contains(box, line) for line in expected) if positive else not displayed)
                complete.append(displayed and all(contains(box, line, 2) for line in expected) if positive else not displayed)
                # Report production display coverage separately from raw
                # segmentation extent. ExpandSubtitleBox adds 30 px on each
                # side, 30 px above, and 10 px below before presentation.
                padded = csv_rect(row, "padded_") if displayed else None
                presentation_complete.append(displayed and all(contains(padded, line) for line in expected)
                                             if positive else not displayed)
                if displayed and positive:
                    union = [min(b[0] for b in expected), min(b[1] for b in expected),
                             max(b[2] for b in expected), max(b[3] for b in expected)]
                    areas.append((box[2]-box[0])*(box[3]-box[1]) / ((union[2]-union[0])*(union[3]-union[1])))
                    edge_padding.append([union[0]-box[0], union[1]-box[1], box[2]-union[2], box[3]-union[3]])
            displayed_boxes = [box for box in boxes if box is not None]
            first_complete = next((items[index][1]["capture_frame"] for index, value in enumerate(strict) if value), None)
            max_edge_motion = max(max(box[edge] for box in displayed_boxes) - min(box[edge] for box in displayed_boxes)
                                  for edge in range(4)) if displayed_boxes else 0
            late_growth = sum(any(current[edge] < previous[edge]-2 for edge in (0, 1))
                              or any(current[edge] > previous[edge]+2 for edge in (2, 3))
                              for previous, current in zip(boxes, boxes[1:]) if previous and current)
            padding_limit = max(8, max((line[3]-line[1] for line in expected), default=0) // 2)
            oversized = sum(max(padding) > padding_limit for padding in edge_padding)
            results[cue["id"]] = {
                "split": cue["split"], "contamination": cue.get("contamination", []),
                "positive": positive, "frames": len(items),
                "bar_authority": sum(int(row["bar_authority"]) for row, _ in items),
                "measured": sum(int(row["measured"]) for row, _ in items),
                "measured_complete_strict": sum(bool(int(row["measured"])) and
                    all(contains(csv_rect(row, "measured_"), line) for line in expected)
                    for row, _ in items) if positive else sum(not int(row["measured"]) for row, _ in items),
                "presentation_complete_strict": sum(presentation_complete),
                "displayed": sum(box is not None for box in boxes),
                "held_frames": sum(int(row.get("held", 0)) for row, _ in items),
                "injected_failure_frames": sum(int(row.get("injected_measurement_failure", 0)) for row, _ in items),
                "complete_tolerance_2px": sum(complete), "complete_strict": sum(strict),
                "line_count_matches": sum(int(row["lines"]) == len(expected) for row, _ in items),
                "distinct_displayed_boxes": len(set(box for box in boxes if box is not None)),
                "all_frames_same_box": positive and all(box is not None for box in boxes) and len(set(boxes)) == 1,
                "area_ratio_min_max": [min(areas), max(areas)] if areas else None,
                "edge_padding_order": ["left", "top", "right", "bottom"],
                "edge_padding_min_max": [[min(v[e] for v in edge_padding), max(v[e] for v in edge_padding)]
                                         for e in range(4)] if edge_padding else None,
                "padding_limit_pixels": padding_limit, "oversized_frames": oversized,
                "max_edge_motion_pixels": max_edge_motion, "late_growth_events": late_growth,
                "cut_valid_frames": len(placements),
                "cut_geometry_failures": sorted(set(placement_failures)),
                "distinct_cut_destinations": len(set(placements)),
                "cut_current_bottom_gap_min_max": [min(current_gaps), max(current_gaps)] if current_gaps else None,
                "first_complete_capture_frame": first_complete,
                "first_complete_tolerance_2px_capture_frame": next((items[index][1]["capture_frame"]
                    for index, value in enumerate(complete) if value), None),
                "first_complete_frames_after_onset": first_complete - cue["onset_frame"]
                    if first_complete is not None and cue.get("onset_frame") is not None else None,
                "first_sample_capture_frame": items[0][1]["capture_frame"],
                "first_sample_complete": strict[0],
                "first_sample_complete_tolerance_2px": complete[0],
                "verified_onset_frame": cue.get("onset_frame"),
                "onset_in_replay": cue.get("onset_frame") is not None and start <= cue["onset_frame"] < end,
                "first_sample_frames_after_onset": items[0][1]["capture_frame"] - cue["onset_frame"]
                    if cue.get("onset_frame") is not None else None}
            if cue["split"] != "diagnostic":
                failures = []
                if not all(strict): failures.append("missing_or_incomplete" if positive else "false_positive")
                if positive and any(int(row["displayed"]) and int(row["lines"]) != len(expected) for row, _ in items):
                    failures.append("incorrect_line_count")
                if positive and max_edge_motion != 0: failures.append("box_drift")
                if positive and oversized: failures.append("excess_padding")
                failures.extend(sorted(set(placement_failures)))
                if positive and len(set(placements)) != 1: failures.append("cut_placement_drift_or_missing")
                if failures: acceptance_failures.append({"cue": cue["id"], "failures": failures})
        report["segments"][name] = {"frames": len(rows), "capture_frame_range": [start, end],
                                    "crop_xywh": segment["crop_xywh"], "cues": results}
        print(f"{name}: {len(rows)} frames, {len(results)} annotated intervals, "
              f"{sum(int(row['displayed']) for row in rows)} displayed boxes", flush=True)
        (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        if args.render:
            render_preview(cap, mapped, rows, segment, labels, sample_fps,
                           args.output / (name + "-" + args.render_mode + "-preview.mp4"), args.ffmpeg, args.render_mode)
    cap.release()
    if len(used_injections) != len(args.inject_measurement_gap):
        raise ValueError("Requested segmentation fault was outside every selected replay segment")
    if not report["segments"] or not any(segment["cues"] for segment in report["segments"].values()):
        raise ValueError("Requested replay has no annotated frames; no regression was evaluated")
    if not args.report_only and not any(cue["split"] != "diagnostic" for segment in report["segments"].values()
                                       for cue in segment["cues"].values()):
        raise ValueError("Requested replay contains no clean acceptance annotations")
    report["acceptance_failures"] = acceptance_failures
    report["accepted"] = not args.report_only and not args.diagnostic_bars and not acceptance_failures
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if acceptance_failures and not args.report_only:
        print(f"FAIL: {len(acceptance_failures)} clean cue regressions", file=sys.stderr)
        return 1
    return 0


def render_preview(cap, mapped, rows, segment, labels, sample_fps, output, ffmpeg=None, mode="bbox"):
    """Draw only in an output copy; numerical acceptance uses original pixels."""
    x, y, width, height = segment["crop_xywh"]
    canvas_height = (height + 65) // 2 * 2
    encoder = None
    writer = None
    if ffmpeg:
        encoder = subprocess.Popen([str(ffmpeg), "-hide_banner", "-loglevel", "error", "-y",
                                    "-f", "rawvideo", "-pix_fmt", "bgr24", "-s", f"{width}x{canvas_height}",
                                    "-r", str(sample_fps), "-i", "-", "-an", "-c:v", "libx264", "-preset", "fast",
                                    "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(output)],
                                   stdin=subprocess.PIPE)
    else:
        writer = cv2.VideoWriter(str(output), cv2.VideoWriter_fourcc(*"mp4v"), sample_fps, (width, canvas_height))
        if not writer.isOpened(): raise RuntimeError("Could not open annotated video writer")
    label_map = {cue["id"]: cue for cue in labels}
    cap.set(cv2.CAP_PROP_POS_FRAMES, mapped[0]["capture_frame"])
    current = mapped[0]["capture_frame"]
    try:
        for source_frame, row in zip(mapped, rows):
            while current <= source_frame["capture_frame"]:
                ok, pixels = cap.read()
                if not ok: raise RuntimeError("Cannot decode frame for preview")
                current += 1
            canvas = np.zeros((canvas_height, width, 3), np.uint8)
            original = pixels[y:y+height, x:x+width]
            rendered = apply_cut_paste_preview(original, row) if mode == "cut-paste" else original
            canvas[64:64+height] = rendered
            cue = label_map.get(source_frame["annotation"], {})
            status = "UNANNOTATED" if not cue else cue["split"].upper()
            if cue.get("split") == "heldout": status = "INDEPENDENTLY ANNOTATED"
            if cue.get("contamination"): status += " / ORIGINAL OVERLAY PRESENT"
            cv2.putText(canvas, f'{source_frame["capture_seconds"]:.3f}s  {mode.upper()}  {status}', (10, 24),
                        cv2.FONT_HERSHEY_SIMPLEX, .6, (255,255,255), 1, cv2.LINE_AA)
            cv2.putText(canvas, f'bars={row["bar_authority"]} measured={row["measured"]} displayed={row["displayed"]} lines={row["lines"]} cut={row["cut_valid"]}',
                        (10, 50), cv2.FONT_HERSHEY_SIMPLEX, .6, (255,255,255), 1, cv2.LINE_AA)
            if mode == "bbox" and int(row["displayed"]):
                left, top, right, bottom = csv_rect(row, "padded_")
                cv2.rectangle(canvas, (left, top+64), (right-1, bottom+63), (0,255,0), 3)
            if encoder: encoder.stdin.write(canvas.tobytes())
            else: writer.write(canvas)
    finally:
        if writer: writer.release()
        if encoder:
            encoder.stdin.close()
            if encoder.wait(): raise RuntimeError("Preview encoder failed")


if __name__ == "__main__":
    raise SystemExit(main())
