# Scope matte verification and subtitle stutter, 2026-11-02

## Reproduced scope rejection

The screenshot at 20:42:23.960Z (447675AF G2 O290 S293) shows current and retained
source detection 2.38:1, [0,273,3840,1884], while AR in use remains held 16:9.
This is crop verification/admission failure, not a request to fit detection to a
2.35 screen. The 51 complete native v210 frames from the interrupted recording
`analysis-20261102-153904-30576-1` all reproduce correct measured boundaries and
PROVISIONAL classification with an unverified vertical crop. The interrupted
capture lacks a final summary; every preserved .bin is complete (22,118,400 bytes)
and has its original sidecar. All 938 source-picture trace observations nominate
the same physical rectangle, floor 64/cutoff 88. No frames were fabricated.

Native bar luma is 64-68, with sparse chroma departures of 6-7 canonical ten-bit
codes. Strict four-code equality in the native matte verifier rejects a few
samples in a quarter, even though the geometry is independently measured. A
boundary row at y1884 also contains luma/chroma filtering fringe which must be
retained rather than called clean black.

## Repair and preservation

For known source range, a measured matte sample can vary by four luma codes and
eight chroma codes (two eight-bit-equivalent chroma steps). Unknown range keeps
the original four-code comparison. Quiet near-neutral outer-reference qualification
is unchanged. Native axis verification and subtitle-masked complete-bar profiles
share this comparison, so ownership cannot encounter a different rounding rule.

Sparse near-black chroma allowance never dismisses coherent picture: connected
picture continuation remains a veto, including chroma departures over four codes;
broad colored rows remain contradiction/collar evidence even below eight codes.
The masked profile similarly rejects coherent colored quarters. Collar retention
keeps its conservative four-code departure rule and retains actual fringe pixels.
Outward chroma alignment is counted in the pre-existing total collar allowance
before verification. Physical detection stays unrounded and separate from the
conservative retained rectangle. No screen/standard ratio or temporal hold is added.

An initial permissive candidate passed the new scope corpus but failed the existing
full-width chroma-collar safety test. The corrected version preserves that collar,
adds a six-code coherent-collar regression, and passes all 1043 mandatory CPU tests
plus 51/51 actual source frames with trusted crop acceptance explicitly required.
The private replay option VP_SOURCE_PICTURE_CAPTURE_REQUIRE_TRUSTED_CROP now checks
BAR_CROP_TRUSTED and bounded outward retention, rather than accepting correct
physical nomination alone. The independent measurement-only replay remains available.

## Stutter evidence and new feedback

The separately preserved playback interval has real output gaps of 5.59 s in
Classic and 5.13 s in Reconstruction; DeckLink delivery remains about 23.976 fps
with no skipped input. Callback/enqueue gaps in the output trace compare selected
outputs and must not be misread as capture-producer stalls. Successful render work
is generally below a millisecond and preparation usually around 8 ms, but these
success-only timings cannot explain early returns without a submission. This
source verification repair is not yet a demonstrated fix for stutter.

The captured interval writes about 9.17 MB over 152 seconds, peaking near 111 KB/s.
Disk bandwidth alone is an unlikely cause; formatting, scheduling and an actual
logging on/off comparison remain distinct. Do not assert logging is cost-free.

New source-crop-verification records contain the exact nominee, conservative
result, axis outcomes, initial mismatch coordinate, selected matte reference and
current sampled YUV at that coordinate. Counters 0/1 are vertical/horizontal
verification states; 2 sample count; 3 classification. Metrics 0/1 mismatch X/Y;
2..4 reference Y/U/V; 5..7 current sample Y/U/V. Flags 1 verification complete,
2 trusted crop, 4 sample available, 8 qualified reference. A repaired initial
mismatch can lie inside the final retained collar; it is not a final discarded-
band failure.

New presentation-skipped records cover successful render calls that submit
nothing. They retain exact source identity, mode, geometry classification,
selected temporal packet, caption/geometry/Classic-fit flags and total attempt
milliseconds, including work omitted by successful-output timing. Specific reasons
mark source/canvas pending, temporal output pending and missing selected mapping;
other paths are explicitly unclassified-no-submit. File logging aggregates skips
at two-second intervals. This records the existing behavior without changing
scheduling, output selection or treating skipped output as successful presentation.

Private pixels, screenshot, traces and failed candidates are under
E:\codex\videoprocessor\evidence\classic-subtitle-stutter-20261102-1536.
The scope repair needs full Release qualification/deployment and live acceptance;
subtitle stutter needs comparison in both modes using the new evidence.


## Qualification isolation

A direct renderer-build check invoked the repository's full qualification hook
while the previous live player was still running. Its standard lane passed
1058/1059 but the persistent decoded-DV caption startup test failed: source
measurement/verification exceeded its 24 ms deadline on some inputs. This is
retained as failed evidence rather than counted as qualification. With the live
player closed, the unchanged isolated test passes (2m57s). The final clean-source
qualification must run serially without the player. This does not establish the
cause of the user's playback stutter.
