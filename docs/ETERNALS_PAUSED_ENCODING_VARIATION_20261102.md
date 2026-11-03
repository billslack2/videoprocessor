# Paused Eternals: source encoding phases and dark-terminal proof

The user clarified that the detected-AR label drops out; the movie does not resize.
The 20-second diagnostic retains full-raster presentation. Native captures contain
four exact repeating variants, 24 instances each, with luma changes of -4/0/+4 in
canonical ten-bit codes. The qualified deployed CPU detector measures full native
3840x2160 for one variant and returns Partial for the other three. This is not a
subtitle, fitting or physical-screen constraint. No aspect is forced to 2.35:1.

## Repair

The source sampler decodes captured v210 without an extra rounding conversion.
The rendered RGB screenshot has already been scaled and color-converted; it is
useful for visual context but its codes are not detector YUV threshold values.
The weak terminal proof sampled every second native pixel at one distance. That
can alias an encoding phase. It now integrates contiguous 16-pixel patches at
three bounded source distances, with distributed source-region agreement and the
existing strong long-depth body continuity and three other raster contacts.

Early contrast remains independent evidence. Later body gradients cannot prove
contact through a flat raised matte. A separate early spatial score and delayed-
rise guard reject noisy matte prefixes. Scores are agreement measures, not claims
of independent-sample probability. Source quantization is not a reason to blindly
lower all black/edge thresholds or reuse an old measured rectangle.

A new noisy-matte counterexample also exposed black-reference sampling alias:
a single perimeter phase could sample 108 rather than 104 and reset the modeled
floor to nominal black. Native L-shaped footprints cover along-edge and inward
variation. The dark-reference cutoff allows one eight-bit-equivalent code (4 in
canonical ten-bit units), and its threshold cannot lie below its modeled floor.
Bright perimeter content still falls back to nominal source black. Observed low-
bit spacing is diagnostic only: matrix conversion can populate low bits while
source rounding continues to vary by several codes.

This uses current source pixels only. It adds no temporal hold, fabricated fresh
measurement, caption authority, confirmation vote, screen-ratio input or standard
aspect nomination. Exact native bounds and separate discard verification remain.
The existing strong-source work bound remains enforced.

## Regressions and evidence

Four new mandatory tests cover a weak continuous edge over four rounding phases,
P210/P010/v210/8-bit YUV layouts, real quantized and noisy flat matte prefixes,
and RGB/YUV conversion with Rec.709/BT.2020, native limited r210 and full RGB8.
The private corpus is replayed through the actual detector plus crop verifier;
its retained sidecar crop is not input to source measurement. The corpus and all
failed attempts remain private at
E:\codex\videoprocessor\evidence\eternals-paused-20261102-1323.

Initial source replay: 4 inputs, 3 mismatches. The first integrated candidate
passes all 96 captured frames and 1,031 native tests but fails the newly added
noisy-matte case; it is not qualified. After independent early evidence and
black-reference fixes, the focused capture/matte/format cases pass, followed by
the RGB range-conversion case. Full matching Release qualification and live
acceptance are recorded separately; no partial lane qualifies deployment.

## Diagnostic source-picture-active fields

Existing metrics 0/1 remain measurement and lookup milliseconds. New metrics:
2 modeled black floor; 3 black threshold; 4 early mean contrast; 5 early score;
6 integrated mean contrast; 7 integrated score; 8..11 regional mean contrasts;
12 source range; 13 source color space; 14 late mean contrast; 15 observed code
spacing (capped at 4). Scores saturate at 1e6 for uniform positive support.
Counters 3 = attempted side + 1 (zero means none); 4 = supported rays;
5 = supported regions; 6 = canonical analysis format; 7 = source encoding.
Flags 128/256 mean dark-terminal proof evaluated/accepted. These are observations,
not new measurement permissions. They make reference, format and gate differences
visible without decoding captured frames again.
