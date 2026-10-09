"""Synthetic typography regression for the production subtitle replay probe.

Uses Windows Arial and supplied bar boundaries; this is not a source-video replay.
The two dialogue dashes must stay with their own rows at every UHD sampling phase.
Requires Pillow and numpy on the development machine only.
"""
import argparse
import csv
import io
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--font", type=Path, default=Path("C:/Windows/Fonts/arial.ttf"))
    parser.add_argument("--sizes", type=int, nargs="+",
                        default=(80, 90, 96, 100, 110, 120, 130, 140, 150, 160, 180),
                        help="Font sizes to verify; defaults include accessibility-sized captions")
    args = parser.parse_args()
    cases = 0
    for size in args.sizes:
        font = ImageFont.truetype(str(args.font), size)
        for phase in range(4):
            image = Image.new("RGB", (3840, 2160))
            draw = ImageDraw.Draw(image)
            expected = []
            picture_bottom = 1884
            row_gap = max(5, round(size * 0.06))
            lower_top = picture_bottom - round(size * 0.35)
            upper_top = lower_top - (font.getbbox("Hg")[3] - font.getbbox("Hg")[1]) - row_gap
            for text, top in (("- They've knocked out all our communications.", upper_top),
                              ("- Do you have transports?", lower_top)):
                bounds = draw.textbbox((0, 0), text, font=font)
                x = (3840 - bounds[2]) // 2
                y = top + phase - bounds[1]
                draw.text((x, y), text, font=font, fill="white")
                expected.append(draw.textbbox((x, y), text, font=font))
            rgb = np.asarray(image)
            frame = np.dstack((rgb[:, :, ::-1], np.full((2160, 3840), 255, np.uint8)))
            run = subprocess.run([str(args.probe.resolve()), "3840", "2160", "0",
                                  "--diagnostic-bars", "276", "1884"],
                                 input=frame.tobytes(), capture_output=True, check=True)
            row = next(csv.DictReader(io.StringIO(run.stdout.decode())))
            actual = tuple(int(row["measured_" + edge]) for edge in ("left", "top", "right", "bottom"))
            union = (min(b[0] for b in expected), min(b[1] for b in expected),
                     max(b[2] for b in expected), max(b[3] for b in expected))
            # Font metrics include a few blank advance pixels at the outer edges.
            assert int(row["measured"]) and int(row["measured_lines"]) == 2, (size, phase, row)
            assert (actual[0] <= union[0] + 8 and actual[1] <= union[1] + 4 and
                    actual[2] >= union[2] - 8 and actual[3] >= union[3] - 4), (size, phase, actual, union)
            cases += 1
    print(f"Passed {cases} large-font dialogue cases ({len(args.sizes)} font sizes, four UHD sampling phases)")


if __name__ == "__main__":
    main()
