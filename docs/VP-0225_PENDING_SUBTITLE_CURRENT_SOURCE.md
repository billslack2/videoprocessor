# Current video during pending subtitle processing

VP-0225 and VP-0222 integration, 2026-11-02.

## Failure being repaired

The deployed combined source 53058498 retains a post-RC6 whole-source subtitle
canvas gate introduced by 01c1e13f. Pending inspection, a positive caption whose
processed output is not ready, or an infeasible fixed-size placement can consume
fresh input while holding the last submitted whole picture. The live incident
contains a 5.796-second Classic wait across sources 201 through 338. An intervening
configuration/native-context reset is recorded; volume causality is not established
by the logs. The user also reports recurring stalls with dense subtitle activity.

A separate Off-mode defect alternates FIT top 190 with raw-safe top 138 while the
physical measurement remains unchanged. A narrower confirmed FIT was suppressing
current raw visibility. That precedence is corrected without changing detection.
The integrated UNKNOWN pan path also now follows an advancing current controller
instead of pinning a prior pose; current raw bounds still clamp it.

## Pending processing is not a video submission veto

Ready processed subtitle canvases keep their existing behavior. A pending canvas
instead requests an explicitly unprocessed, output-only presentation of the current
source. It does not borrow an older complete subtitle frame, invent absence, erase
raw captions, or acquire new detected/native/full-raster authority.

An independently source-bound raw visibility witness carries direct-sample status,
source generation/sequence, viewport epoch, format, analysis domain, native base and
visible bounds. When it matches a valid accepted native base, the raw presentation
contains the union of that base and current visible pixels, aligned outward and
bounded by the input raster. This reuses completed retention evidence; it adds no
new detector or text scan. If the witness/base is unavailable or stale, complete
current input pixels are the conservative presentation. This is not evidence of a
full-raster active picture and must not update accepted native aspect or boundaries.

Ordinary optional aspect fill must not trim those protected pixels. Explicit user
fixed-aspect cropping remains an explicit presentation setting. Caption observation
state remains as observed; raw fallback has no processed/native/FIT ownership proof.

Temporal output must select the exact current raw source, bypass previous subtitle
packet repeats and mixing, and preserve required source conversion/deinterlacing.
Raw processing must not reuse a cleaned/composed cache entry. Geometry/admission
handling must distinguish this output-only state from native picture acquisition.

## Evidence and tests

Native tests cover identity/base faults, current raw coverage, full-input fallback,
no authority mutation, ready/OFF/fixed-setting controls, Off FIT precedence, and
controller progression. Actual renderer checks must join fallback admission to the
submitted original source and verify caption/source pixels, current source progress,
and no stale composition or repeated whole picture under caption/overlay load.
Both shipped dependencies and motion paths remain required. No passing qualification
or live-scene acceptance is claimed by this design note.

Trace flag 32 identifies raw subtitle fallback; flag 64 identifies an exact independent
raw visibility witness. On those rows only, counters 3..7 are witness source sequence,
source generation, viewport epoch, source format key and analysis domain. Metrics
8..11 contain raw bounds and previous[] contains its native base. Candidate presence
retains flag 8 and engage-pose retention flag 16. Ordinary row meanings are unchanged.

## Static cue reuse and all unprocessed source selection

The persistent-caption counterexample is a remembered-pixel check at (1548,1886),
four pixels outside glyphs beginning x1552. Exact background equality over an
expanded line rectangle is not cue identity. The pending guard now supports a
bounded current-source ink verifier seeded from an accepted owned-glyph template.
It samples the configured subtitle bands, verifies current known glyphs and black
support, and vetoes new ink, alternate-color/local-contrast marks, and tiny adjacent
changes. Raw/fringe exemptions require tightly matching seed YUV. Incomplete or
over-budget live palette proofs cannot fall through to a smaller legacy ROI check.
The sample cap is 327,680; no connected-component recognition is added to this
current-source verification path. Source masks are rebuilt from current pixels,
remain pending/non-semantic, and preserve the original hold budget and seed age.

Every unprocessed canvas decision, including the older native-size continuation,
now carries a separate exact-current source marker into temporal selection. It
cannot select an older complete movie/subtitle packet. This marker is distinct
from diagnostic bit32, which still identifies only the explicit union/full-input
geometry fallback. Current color/deinterlace processing and policy geometry remain.

Initial shader preparation must also preserve a known positive outboard caption;
only proven-contained current raw pixels may remain in the native aperture while
processing is pending. Before the first Classic pose, explicit source-joined raw
fallback may preserve picture plus caption at a larger envelope. Once a processed
or settled cue is acquired, existing steady viewport/cadence/pixel checks stay strict.

## Timing review boundaries

The 327,680 ceiling bounds unique source reads, not elapsed render-thread time.
At UHD the grid is 960 by 540; both search bands include encoded bars as well as picture-side search depth. For
a 276/1884 UHD picture they contain about 280,320 points at
zero near-bar distance and 289,920 at 20 source pixels. Wider searches may exceed
the existing sample cap and safely decline reuse. Seed creation also
walks masks/support, and current proofs inspect cached neighbors. Accepted current
measurements can recreate the seed, so seed cost must be measured as well as the
late-worker branch. The earlier guard capped expanded line-window reads at 32,768.
Do not claim this broader verification is faster solely because work is bounded.

The independent local-contrast veto is conservative. Textured movie pixels in the
search bands can reject continuation even when the cue is unchanged. Such rejection
must lead to current raw video, never waiting or reuse of older movie pixels. Uniform
moving-background positives do not establish successful reuse over arbitrary movie
texture. Report verifier time and rejection reasons with actual UHD detector seeds,
and distinguish a safe rejected proof from successful static-cue reuse.