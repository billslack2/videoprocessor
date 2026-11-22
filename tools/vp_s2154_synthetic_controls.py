"""Create private synthetic controls for vp_s2154_projection_replay.

The input must be the private S2154 or S2173 P010 capture. These controls use
only its clipped glyph-shaped pixels over an artificial flat, neutral matte.
They do not establish ownership of the original source, nor test its omitted
anti-aliased fringe. Output files contain private picture pixels; do not commit.
Requires numpy. Use an output directory outside the source tree.
"""
import argparse
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('capture', type=Path)
p.add_argument('output_dir', type=Path)
p.add_argument('source', choices=('2154', '2173'))
a = p.parse_args()
raw = np.fromfile(a.capture, dtype='<u2')
if raw.size != 3240 * 3840:
    p.error('expected a 3840x2160 P010 capture')
raw = raw.reshape(3240, 3840)
expected = 150 if a.source == '2154' else 156
if int(raw[2049, 1431] >> 6) != expected:
    p.error('capture does not match selected source')
a.output_dir.mkdir(parents=True, exist_ok=True)
original = raw[:2160].copy()
raw[:260, :] = 119 << 6
raw[1898:2160, :] = 119 << 6
raw[2008:2048, 1215:2442] = original[2008:2048, 1215:2442]
raw[2160:2290, :] = 512 << 6
raw[3109:3240, :] = 512 << 6
for name, level in (('clean', None), ('weakmark', 126), ('brightmark', 700)):
    if level is not None:
        raw[1980:1988, 25:33] = level << 6
    out = a.output_dir / f's{a.source}-synthetic-{name}.bin'
    raw.tofile(out)
    print(out)
    if level is not None:
        raw[1980:1988, 25:33] = 119 << 6
