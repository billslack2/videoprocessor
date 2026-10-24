"""Read the earlier recording around its two-frame subtitle disappearance.
Requires OpenCV and NumPy. No native or recorded pixels are exported.
This diagnostic is not a brightness verdict or a replay of the 21:26 incident.
"""
import argparse
import hashlib
import json
from pathlib import Path
import cv2
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("--video", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
cap = cv2.VideoCapture(str(args.video))
frames = {}
for n in range(1316, 1325):
    cap.set(cv2.CAP_PROP_POS_FRAMES, n)
    ok, frame = cap.read()
    if not ok:
        raise RuntimeError(f"Cannot read frame {n}")
    frames[n] = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
cap.release()
mask = np.zeros_like(frames[1316])
mask[405:1000, 600:2480] = 255  # scene, excluding subtitle band and VP UI
results = []
for a, b in [(1316, 1318), (1318, 1319), (1319, 1320), (1320, 1321), (1321, 1324), (1318, 1321)]:
    f, g = frames[a], frames[b]
    q = cv2.goodFeaturesToTrack(f, 2000, 0.01, 12, mask=mask)
    if q is None:
        raise RuntimeError("No scene features")
    r, st, _ = cv2.calcOpticalFlowPyrLK(f, g, q, None, winSize=(31, 31), maxLevel=3)
    keep = st[:, 0] == 1
    q, r = q[keep, :, :], r[keep, :, :]
    mat, inliers = cv2.estimateAffinePartial2D(q, r, method=cv2.RANSAC,
        ransacReprojThreshold=1.0, maxIters=4000, confidence=.999)
    if mat is None:
        raise RuntimeError("No robust scene transform")
    stats = []
    for x, y in q[inliers[:, 0] == 1, 0, :]:
        x, y = int(round(x)), int(round(y))
        v = f[y-2:y+3, x-2:x+3].astype(float)
        w = g[y-2:y+3, x-2:x+3].astype(float)
        if v.shape == (5, 5) and 8 < v.mean() < 240:
            stats.append((v.mean(), w.mean()))
    z = np.array(stats)
    results.append(dict(from_frame=a, to_frame=b, from_seconds=a/60, to_seconds=b/60,
        inliers=int(inliers.sum()), tracked=len(q), scale=float(np.hypot(mat[0,0], mat[0,1])),
        tx_pixels=float(mat[0,2]), ty_pixels=float(mat[1,2]), patches=len(stats),
        median_patch_delta_8bit=float(np.median(z[:,1]-z[:,0])),
        median_patch_ratio=float(np.median(z[:,1]/z[:,0]))))
report = dict(source_video=str(args.video), video_sha256=hashlib.sha256(args.video.read_bytes()).hexdigest(),
    incident="Earlier recording; not the 21:26 replay", roi=[600,405,2480,1000],
    method="LK tracks; RANSAC similarity transform excludes subtitle band; local unregistered 5x5 patch photometry",
    subtitle_missing_frames=[1319,1320], results=results,
    qualification="Similarity fit tests scene scale in this interval. Unregistered patch photometry is diagnostic, not a brightness or HDR verdict.")
args.output.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
print(json.dumps(report, indent=2))
