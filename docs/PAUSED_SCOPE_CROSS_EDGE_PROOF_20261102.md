# Combine native edge evidence before declaring an obvious rectangle unknown

The paused input recorded at 20:42 on 2026-11-02 has 41 native v210 frames.
The deployed 788554a1 detector fails all 41. Its top boundary has 71 probes,
both raster sides have 96, and bottom row 1884 has 17 exact native edge votes
spread 7/7/0/3 across the width. Requiring 32 votes on every inset discards this
coherent rectangle. The screenshot's retained ratio alone is not current proof.

## Conservative current-source corroboration

After the complete competing-edge scan, exactly one still-unqualified side can
use both independently selected perpendicular edges and its selected opposite.
The local candidate needs a strong native profile, at least 12 real probes in
three substantial quarters and at most two native coordinates of uncertainty.
Both perpendicular contacts must reach its coordinate within one existing ray
spacing and have no positive contact beyond it. Distinct surviving candidates
remain ambiguous. A competing qualified boundary is never overridden.

Original probe counts, coordinates and bitsets are unchanged. This is additional
independent current evidence, not artificial votes, mirrored bars or a preferred
standard AR. Existing component reconciliation and independent raw/masked crop
verification still apply. The repair introduces no luma/chroma threshold change.

## Remembered ratios and alternative hypotheses

Remembered picture bounds already provide inspection/continuity candidates.
They are useful hypotheses; current native contacts must confirm them. An old
16:9 or 1.90 picture must yield to new boundary evidence, and a familiar scope
ratio must yield to actual continuing full-raster picture. No screen/zoom/CIH
constraint can select a detected ratio. This repair needs no history to measure
the captured frame; it does not add a new ratio-history bank or let retained
geometry masquerade as current measurement.

The actual-renderer transition fixture now establishes full raster, then 1.90,
then a shadowed scope edge with 17 local probes. The scope submission must join
its own cross-edge proof. Subsequent real transitions remain observable.

## Feedback records and tests

`source-perpendicular-inset` flags: 1 accepted, 2 ambiguous. Counters: side,
local support, substantial quarters, eligible candidates. Metrics: coordinate,
minimum, maximum, existing ray spacing, the two nearest perpendicular contacts,
and their respective outside-support counts. Accepted records always name the
accepted candidate; rejected records describe the highest-supported candidate.

Synthetic cases cover all four orientations, asymmetric nonstandard bounds,
several coordinate phases, captions, continuing picture beyond a proposed edge,
a perpendicular side ending short, too few supported regions and detached glyphs.
The private replay requires current measured bounds and independently trusted crop
on all 41 source frames within the real bounded worker. Existing sun/starfield,
paused full-raster, dark-caption and scope-logo corpora remain regression inputs.
No private movie pixels are committed. Full Release qualification and both motion
suites remain deployment gates; active configuration is preserved.

## Returning Classic engage stability exposed during qualification

The first full run passed all 1065 standard checks but failed one beta return-to-
Classic case. Native picture bounds remained 250–1906. The routed controller
advanced 16 to 24 pixels, but a conservative pending caption extent projected a
64-pixel pan before a fresh extent allowed 60, producing a four-pixel backslide.

During same-side engage, a matching already submitted legal viewport now remains
an effective constraint until the configured controller reaches it. The pose must
match native owner, format, domain, epoch, dimensions and presentation certificate,
and stay inside the current safe interval. Foreign or invalid poses do not constrain
it; release clocks and actual native geometry are unchanged. `engage_retained` and
flag 8 of `subtitle-canvas-admission` explain this presentation-only constraint.
A deterministic top/bottom regression first fails on the old behavior and checks
all of those context guards. This is separate from the detector's current edge proof.
