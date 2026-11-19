"""Measure hard top/bottom AR cuts against VP's renderer trace (requires NumPy).

Usage: python tools/check_ar_transition.py CAPTURE_DIRECTORY [...]
Positive lag means the layout changed late, in source frames. The displayed
field is VP's layout record; this tool does not measure physical scanout.
"""
import argparse
import json
from pathlib import Path
from statistics import median

import numpy as np


IDENTITY = ("transportGeneration", "acceptedSequence", "sourceFrameNumber",
            "captureTimestamp", "sourceFormatGeneration", "viewportGeneration",
            "rendererGeneration", "analysisDomainKey")


def bar_depth(strip):
    # Sixteen interior lanes. A local outer-edge reference also handles raised
    # matte. Reject noisy/ambiguous exteriors rather than guessing a boundary.
    floor = np.median(strip[:8], axis=0)
    noise = np.percentile(abs(strip[:8] - floor), 90, axis=0)
    usable = (noise <= 8) & (abs(floor - np.median(floor)) <= 12)
    if np.count_nonzero(usable) < 12:
        return None
    changed = abs(strip[:, usable] - floor[usable]) > np.maximum(10, 3 * noise[usable])
    picture = np.mean(changed, axis=1) >= 0.67
    starts = np.flatnonzero(picture[:-2] & picture[1:-1] & picture[2:])
    return int(starts[0]) if starts.size else None


def read_frame(path):
    header, identity = path.with_suffix(".txt").read_text().splitlines()[:2]
    width, height, stride, _, fmt, *_ = map(int, header.split())
    tag, *fields = identity.split()
    if tag != "identity_v1" or fmt not in (0, 1, 4):
        raise ValueError("requires identity_v1 and P010, P210, or decoded DV pixels")
    values = tuple(map(int, fields))
    key = values[:8]
    if stride < 2 * width or stride % 2 or height < 64 or width < 32:
        raise ValueError("invalid or unsupported luma raster")
    y = np.memmap(path, dtype="<u2", mode="r", shape=(height, stride // 2))
    lanes = (np.arange(16) * 2 + 1) * width // 32
    top = bar_depth((y[:height//4, lanes] >> 6).astype(float))
    bottom = bar_depth((y[:-height//4-1:-1, lanes] >> 6).astype(float))
    tol = max(4, height // 180)
    depth = None if top is None or bottom is None or abs(top-bottom) > tol else (top+bottom)/2
    return key, height, tol, depth


def check(directory):
    layouts = {}
    with (directory / "analysis.jsonl").open(encoding="utf-8") as records:
        for line in records:
            r = json.loads(line)
            if r["stage"] == "presentation":
                key = tuple(r[k] for k in IDENTITY)
                layouts.setdefault(key, set()).add(tuple(r["displayed"]))
    frames = []
    for path in sorted(directory.glob("frame-*.bin")):
        key, h, tol, depth = read_frame(path)
        matches = layouts.get(key, set())
        shown = None
        if len(matches) == 1:
            _, t, _, b = next(iter(matches))
            if 0 <= t < b <= h:
                shown = (h - (b-t))/2  # translation preserves height
        frames.append((key, depth, shown, tol))
    print(f"{directory.name}: {len(frames)} input frames")
    found = 0
    # Require two old and three new independent pixel observations. The event
    # is dated to the first new frame; future evidence adds no playback delay.
    for i in range(2, len(frames)-2):
        window = frames[i-2:i+3]
        depths = [f[1] for f in window]
        tol = frames[i][3]
        if None in depths:
            continue
        old, new = median(depths[:2]), median(depths[2:])
        if (max(depths[:2])-min(depths[:2]) > tol or max(depths[2:])-min(depths[2:]) > tol
                or abs(old-new) < 4*tol):
            continue
        found += 1
        keys = [f[0] for f in window]
        def adjacent(a, b):
            return (a[0] == b[0] and a[4:] == b[4:] and b[1] == a[1]+1
                    and b[2] == a[2]+1 and b[3] > a[3])
        label = f"  source {keys[2][1]}: top/bottom depth ~{old:g} -> {new:g}"
        if not all(adjacent(a,b) for a,b in zip(keys,keys[1:])):
            print(label, "inconclusive (capture gap/reset)")
            continue
        def state(j):
            shown = frames[j][2]
            if shown is None:
                return None
            if abs(shown-old) <= tol:
                return "old"
            if abs(shown-new) <= tol:
                return "new"
            return None
        j = i
        if state(i) == "old":
            while j+1 < len(frames) and state(j) == "old" and adjacent(frames[j][0], frames[j+1][0]):
                j += 1
                if frames[j][1] is None or abs(frames[j][1]-new) > tol:
                    break
        else:
            while j > 0 and state(j-1) == "new" and adjacent(frames[j-1][0], frames[j][0]):
                if frames[j-1][1] is None or abs(frames[j-1][1]-old) > tol:
                    break
                j -= 1
        if (j == 0 or j+1 >= len(frames) or state(j-1) != "old" or state(j) != "new"
                or state(j+1) != "new" or not adjacent(frames[j-1][0],frames[j][0])
                or not adjacent(frames[j][0],frames[j+1][0])
                or (j>i and (frames[j][1] is None or abs(frames[j][1]-new)>tol))):
            print(label, "inconclusive (layout switch not bracketed)")
        else:
            print(label, f"layout {frames[j][0][1]}, lag {j-i:+d} source frames")
    if not found:
        print("  Inconclusive: no bracketed large two-sided bar change.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, nargs="+")
    for directory in parser.parse_args().directory:
        try:
            check(directory)
        except (OSError, ValueError, KeyError, IndexError) as error:
            parser.exit(1, f"{directory}: {error}\n")
