# Eternals sparse raster contact repair, 2026-11-02

## Reproduced defect

The later paused sun/starfield scene is a separate failure from the earlier
encoding-phase terminal-gradient issue. The deployed c84a216b detector returns
`bounded-topology-component-limit` on all 95 recorded native v210 frames. Source
interpretation is 3840x2160, stride 10240, limited range, Rec.709. These source
pixels contain picture at all raster extremes; output fitting is not evidence.

The fixed overview remains at most 480x270: in this 4K input it samples one native
pixel near each 8x8 cell centre, without resizing/averaging the picture. The 512
cap counts connected foreground groups, not cells. Stars exceed that cap. The
old exception aborted measurement before independent edge profiles were examined.

Removing only the exception does not repair this capture. Native top contact has
60/128 supporting rays and bottom contact 63/128, concentrated on the same bright
left-hand picture. The old terminal rule requires 64. The right-hand dark field
mostly has ten-bit luma 104-108, but a coherent star at the exact last source
column reaches 268. Fixed 128-ray positions miss that star. Relaxing black luma
thresholds or choosing a standard/screen aspect is not the solution.

Private original pixels, sidecars, diagnostic summaries, replay results and the
rendered screenshot remain outside Git under
`E:\codex\videoprocessor\evidence\eternals-starfield-20261102-1430`.
The native recording is `analysis-20261102-144827-56380-2`; the preceding
`144225` capture contains telemetry only (`includeFrames=false`).

## Boundary-first bounded evidence

1. Collect the cheap current-native terminal probes before component classification.
2. Preserve the fixed overview and 512 classified-component cap. Prioritize
   top/bottom raster contacts, then side contacts, before interior components.
   Finish all retained extents. Label excess occupied cells 0xffff, never sampled
   background; report topology incomplete honestly. Classification stays bounded.
3. Opposite raster contacts sharing at least 32 native rays across two substantial
   quarters can corroborate each other when an independently selected third raster
   contact anchors them. Positive glyph vetoes apply first. This proves actual
   edge contact from local bright content without demanding half the edge be lit.
4. With exactly one unresolved side and three proved raster contacts, inspect a
   narrow native edge strip (6-16 pixels deep, at most 1080 along-edge probe
   positions). A 3x3 patch at the exact raster must have >=6 samples >floor+24 and
   a peak >floor+48. Coherent near-edge texture must occur in three separate
   quarters. Glyphs, including adjacent-cell sampling uncertainty, are excluded.
   A distributed inward onset veto prevents edge marks borrowing a bright body
   behind a thin matte. A lone logo on a flat bar cannot establish this proof.
5. Independently proved full-raster contacts can finish a measurement despite
   incomplete interior topology. No source pixels are discarded. An inset
   candidate still requires complete topology reconciliation and independent
   native discard verification; an incomplete overview cannot authorize a crop.
6. Uncertain cases retain the full competing-boundary scan. Gradient-based dark
   terminal continuity runs only after this scan, preserving its competing-inset
   and delayed-rise guards from c84a216b. No ratio, temporal hold or OSD label is
   forced. CIH, screen aspect, zoom and subtitle presentation are not inputs.

The fast route still reads the bounded coarse overview for subtitle protection.
It skips expensive full-depth competing-boundary scans when current raster
contacts are independently proved; it does not scan every native picture pixel.

## Mandatory regressions and private replay

Six added mandatory CPU methods cover complex interior / independently proved
raster contacts, incomplete-topology inset refusal, corroborated sun plus sparse
stars, flat matte/lone-logo refusal, thin-matte inward onset, and boundary-caption
classification/veto despite interior complexity. The caption case caught an
initial sparse-route glyph omission and a subsequent cell-centre alias; both
failed iterations are retained privately. The minimum native count is 1039.

The complete Native lane passes all 1039 contracts, including all 95 exact native
starfield frames through measurement and verification. That run averages 2.182 ms
and peaks at 3.8893 ms including darkness evaluation. Separate replay through the
actual 24 ms worker passes 95/95 (3.572 ms average, 5.3856 ms peak). The previous
96-frame paused encoding-phase corpus also passes 96/96 with the repaired native
library (3.289 ms average, 5.0859 ms peak). These are controlled run timings, not
universal latency guarantees. Native-lane binaries live in `tests-critical-native`;
manual runs against the older root `VideoProcessor-Test.dll` are baseline evidence,
not evidence for this repair. Full Release qualification remains a separate gate.

## Feedback without re-decoding pixels

`source-picture-active` retains existing fields. Flags 512/1024 mean complete
sampled topology / component budget exceeded. Flags 2048,4096,8192 mean opposed
contacts corroborated / sparse terminal evaluated / sparse terminal accepted.
Flags 16384/32768 mean distributed inward-onset veto / positive glyph veto seen.
Metrics 16..19 are sparse contact peak contrast, coherent contact patch count,
texture-region count, attempted side+1 (zero means none). Metrics 20..23 are
coherent texture counts in each quarter. Counts describe sampled support rather
than independent statistical confidence. Existing metrics 0..15 keep their
reference, format, timing and gradient-proof meanings.

The physical-edge reason now survives early inconclusive returns, so structured
presentation diagnostics can show the actual rejection reason rather than an
empty measurement reason. Record native frames only when a new source ambiguity
requires pixel-level proof; the decision fields support subsequent comparison.
