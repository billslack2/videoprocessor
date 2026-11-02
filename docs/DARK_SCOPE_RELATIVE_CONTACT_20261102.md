# Dark scope edge and subtitle acquisition repair, 2026-11-02

The paused dog/caption input measured three native boundaries but repeatedly
lost the left raster contact. Its 96 captured v210 frames all failed on ce55ed99.
Top and bottom agreed around source rows 276 and 1883. The left terminal had
24/128 bright probes, below the existing global support requirement. Actual
bar luma was 64; the left picture was mostly 76–80, with excursions to 88.
That is positive picture evidence, despite its weak contrast and quantization.

## Current-source relative contact

After the complete competing inset scan, a still-unselected raster edge can
use two independently measured opposite-axis inset bars as references. The
check samples both current outer bars at the same native inward footprint as
the missing contact. References must be quiet, mutually consistent and agree
with the measured perimeter floor. Unknown encoding range cannot use this route.
No nominal black code, desired aspect, retained aspect, or screen size is supplied.

The check retains the existing full-raster 128-ray lattice, excludes rays in the
already measured bars, and requires current positive contrast on at least half
the remaining rays in three picture-span quarters. Each positive ray needs 9/12
native samples. Contrast margins are max(6, reference dispersion + 2) ten-bit
codes. Median patch values preserve native four-code quantization; their
10th–90th percentile span must exceed max(4, twice reference noise + 2).
Flat side mattes and noise matching the current bars cannot establish contact.
A qualified competing inset always wins. This proof only establishes contact
with the raster; it never authorizes removal of a pixel.

All existing topology reconciliation and independent raw/subtitle-masked crop
verification still run. Caption position does not nominate physical bounds.

## Subtitle handoff

Retained picture coordinates already nominate subtitle searches during uncertain
measurement. An inspection-only search cannot grant picture or erasure authority.
The repaired current physical nomination now passes the existing native
subtitle-masked proof, allowing acquisition of the scope picture and subsequent
shared-picture subtitle processing. The old 1.90 retained reference is not forced
into the detector and is not promoted merely because a caption was found.

The private replay requires both detected glyphs and the sampler's own accepted
current proof when raw bars are occluded, plus the independent masked verification.
This is stronger than checking a candidate caption box alone.

## Diagnostics and validation

`source-relative-matte-contact` is a bounded numeric trace record for each evaluated
side. Flags: 1 accepted, 2 matching references. Counters: side, covered rays,
supporting rays, substantial active-span quarters. Metrics 0–2 reference YUV,
3–5 reference dispersion, 6–8 median-patch texture, 9 luma margin, 10 chroma margin.
It uses the existing recorder; no pixel dumps or per-sample text logging are added.
Existing enhanced logging, timecode and incident screenshots were already enabled.
No deployed configuration edit is required.

Native controls cover all four sides, limited/full range, raised source black,
caption presence, flat side matte, mismatched bar references, unknown range and
bar-level quantized noise. Private bounded-worker replays additionally cover the
96 dark/caption frames, 51 scope-logo frames, 95 sun/starfield frames and 96 prior
paused full-raster frames. Movie pixels stay outside source control.

The paused capture recorded no presentation-skipped events. This repair does not
claim to resolve the separately reported moving-playback stutter. Full matching
Release qualification and both renderer motion suites remain deployment gates.
