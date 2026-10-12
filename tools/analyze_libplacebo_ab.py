"""Analyze float outputs from libplacebo_ab_probe.c (requires NumPy).

Run in the probe output directory, or pass its path. The probe itself checks
original/custom/original restoration. This script verifies finite/bounded custom
output and alpha, and writes comparison.csv with qualified hue/chroma/Y metrics.
"""
import argparse
import csv
import json
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path, nargs="?", default=Path.cwd())
args = parser.parse_args()
root = args.directory
rows = list(csv.DictReader((root / "frames.csv").open()))
shape = (len(rows), 73, 128, 4)
original = np.fromfile(root / "frames-original.f32", dtype="<f4").reshape(shape)
custom = np.fromfile(root / "frames.f32", dtype="<f4").reshape(shape)
lines = (root / "matrices.csv").read_text().splitlines()
mats = [np.array([float(v) for v in line.split(",")[1:]]) for line in lines[:2]]
iptmat = np.array([[float(v) for v in line.split(",")] for line in lines[2:]])

def ipt(a,scenario):
    mat=mats[scenario==2][:9].reshape(3,3)
    lms=np.maximum(a@mat.T/10000,0)**(2610/16384)
    pq=((3424/4096+(2413/128)*lms)/(1+(2392/128)*lms))**(2523/32)
    return pq@iptmat.T

def inside(a,p,eps=0):return ((a>=-eps)&(a<=p+eps)).all(axis=-1)
def summary(f,o,p,ycoef,sc):
    touched=np.max(np.abs(o-f),axis=-1)>1e-7
    strong=~inside(f,p,max(.02,p*.0002))
    yi=f@ycoef;yo=o@ycoef
    # RGB distance from same-luminance neutral, normalized by luminance.
    sat=lambda a,y:np.linalg.norm(a-y[...,None],axis=-1)/np.maximum(y,1e-5)
    sf=sat(f,yi);so=sat(o,yo)
    a,b=ipt(f,sc),ipt(o,sc)
    ca=np.linalg.norm(a[...,1:],axis=-1);cb=np.linalg.norm(b[...,1:],axis=-1)
    hue=np.abs(np.degrees(np.arctan2(np.sin(np.arctan2(b[...,2],b[...,1])-np.arctan2(a[...,2],a[...,1])),np.cos(np.arctan2(b[...,2],b[...,1])-np.arctan2(a[...,2],a[...,1])))))
    chromatic=strong&(ca>.001)&(cb>.001)
    newreversal=(np.diff(yo,axis=1)<-max(.001,p*.00001))&(np.diff(yi,axis=1)>=-max(.001,p*.00001))
    return dict(min=float(o.min()),max=float(o.max()),outside=int((~inside(o,p,1e-7)).sum()),changed=int(touched.sum()),strong_input=int(strong.sum()),rgb_saturation_increases=int((strong&(so>sf+1e-4)).sum()),ipt_chroma_increases=int((strong&(cb>ca+1e-5)).sum()),max_ipt_hue_change_deg=float(hue[chromatic].max(initial=0)),max_Y_change=float(np.abs(yo-yi).max()),Y_outside=int(((yi<0)|(yi>p)).sum()),new_luminance_reversals=int(newreversal.sum()),neutral_spread=float(np.ptp(o[72],axis=-1).max()))

stats = []
for i, row in enumerate(rows):
    scenario = int(row["scenario"])
    peak = float(row["target_nits"])
    result = summary(original[i, ..., :3].astype(float) * 203,
                     custom[i, ..., :3].astype(float) * 203,
                     peak, mats[scenario == 2][9:], scenario)
    result["material_outside"] = int(np.any(
        (custom[i, ..., :3] * 203 < -.001) |
        (custom[i, ..., :3] * 203 > peak + .001), axis=-1).sum())
    stats.append(dict(**row, **result))
with (root / "comparison.csv").open("w", newline="") as file:
    writer = csv.DictWriter(file, stats[0].keys())
    writer.writeheader()
    writer.writerows(stats)
for scenario in range(3):
    selected = [s for s in stats if s["scenario"] == str(scenario)]
    print(json.dumps(dict(
        scenario=scenario, frames=len(selected),
        outside=sum(s["material_outside"] for s in selected),
        max_hue=max(s["max_ipt_hue_change_deg"] for s in selected),
        chroma_increases=sum(s["ipt_chroma_increases"] for s in selected),
        new_Y_reversals=sum(s["new_luminance_reversals"] for s in selected))))
assert np.isfinite(original).all() and np.isfinite(custom).all(), "Non-finite output"
assert np.array_equal(original[..., 3], custom[..., 3]), "Alpha changed"
assert all(s["material_outside"] == 0 for s in stats), "Custom output exceeds bounds"
print(f"PASS {len(rows)} frames: finite output, unchanged alpha, RGB bounds at 0.001 nit")
