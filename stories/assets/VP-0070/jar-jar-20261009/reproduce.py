"""Replay a screenshot approximation and bounded exterior-shadow perturbations.
Requires numpy, opencv-python and a native replay harness built from the candidate.
These SDR screenshots are not the original P210 capture and do not measure native performance.
"""
import argparse, subprocess
from pathlib import Path
import cv2
import numpy as np
p=argparse.ArgumentParser()
p.add_argument("--replay",required=True)
p.add_argument("--output",required=True)
a=p.parse_args(); out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
image=cv2.imread(str(Path(__file__).with_name("off.png")),cv2.IMREAD_COLOR)
assert image is not None
xs=3840/1980;ys=1608/830
matrix=np.array([[xs,0,-4*xs],[0,ys,276-132*ys]],dtype=np.float64)
base=cv2.warpAffine(image,matrix,(3840,2160),flags=cv2.INTER_LINEAR)
report=[]
for name,shadow in [("original",None),("shadow1580",1580),("shadow1612",1612),("shadow1650",1650),("shadow1700",1700)]:
    frame=base.copy()
    if shadow is not None:frame[shadow:1884,1212:1242,:]=0
    bgra=cv2.cvtColor(frame,cv2.COLOR_BGR2BGRA)
    path=out/(name+".bgra");path.write_bytes(bgra.tobytes())
    result=subprocess.check_output([a.replay,str(path),"3840","2160","276","1884"],text=True)
    report += [name,result]
    lines=[line for line in result.splitlines() if "detected=" in line]
    assert len(lines)==2 and all("detected=1 lines=3" in line for line in lines),result
    for line in lines:
        bounds=line.split(" bounds=")[1].split(" ")[0]
        upper=int(bounds.split("-")[0].split(",")[1])
        assert upper<=1534,(name,line)
(out/"report.txt").write_text("\n".join(report),encoding="utf-8")
print("All five replay variants retain the top glyph row at both bar edges.")
