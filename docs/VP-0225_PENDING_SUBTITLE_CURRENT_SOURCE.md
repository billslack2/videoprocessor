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
## Current prepared-footprint runtime path

Advanced modes seed pending reuse only after an actual current composition has
established its extraction and cleanup geometry. A separate guard shares the
existing native-pixel glyph/core/fringe and backing verifier with cadence logic;
the cadence entry point retains its stride-two and single-skip rules. Pending
ordering, backlog reassociation and lease accounting remain in the pending guard.

Changed unowned nonblack movie pixels outside the foreground/content hull can
restrict a rectangular cleanup on any of its four edges. A one-use receipt binds
the current source, original geometry, policy and physical base. The compositor
must consume that receipt before processing. The original rectangle minus the
restricted inner rectangle is explicitly protected: cleanup and panel background
cannot replace those current pixels; verified current glyph alpha may still draw.
The original donor exclusion and placement/layout coverage remain unchanged.
Interior changes, unsupported contours, changed glyphs/backing, or unbound geometry
fail into current raw video. Nonblack or chroma-bearing encoded-bar pixels outside
the verified operation also veto reuse, so native cropping cannot hide new bar ink.

The pending result creates no new bar/native, caption-absence or full semantic
measurement. It retains current source identity and does not renew its accepted
seed from held/pending frames. No historical movie pixels enter rendered output.
The earlier global picture-contrast verifier remains a generic path; the advanced
renderer uses the prepared-footprint path so unrelated moving movie texture is not
interpreted as cue novelty. The earlier 8.5-15.4ms full-band probe is exploratory
work on that generic path, not a measurement of this runtime path.
## Ready-frame template work and band-edge correction

A static cue is not a new semantic cue merely because a new source frame arrives.
The pending guard spends the original elapsed lease and never renews it from its
own output. Full current measurements may establish a new accepted source, while
immutable footprint topology can be reused only with matching geometry, context,
layout and detector masks. Every output still reads current movie/glyph pixels.

The current ready path invokes both cadence and pending-footprint seed guards.
Their lifetime rules differ, but their footprint construction overlaps. The new
`subtitle-cadence-seed` and `subtitle-footprint-seed` records measure the complete
calls, including setup; `subtitle-pending-seed` operation time is metrics[3], not
metrics[0] (which is lease age). Timings must include rejected attempts and report
call counts, successful reuse counts and source progression. A cheap pending
branch does not establish a cheap pipeline if accepted frames repeatedly rebuild
masks or duplicate footprint setup. Measure before sharing topology or changing
scheduling; keep independent cadence/pending lifetime and reset fences.

A generic current proof also needs accepted glyph samples that fall outside the
current search bands. It now explicitly reads those exact accepted coordinates,
checks original YUV and palette, and refreshes only partial output raw/owned bits.
This cannot become a full detector scan or semantic/absence/physical proof.
A paired test changes an out-of-band first-line sample and requires rejection.

Full qualification at fffc06f3 failed; steady raw fallback can expand presentation
while the accepted physical model stays fixed. Initial unknown-cue preservation
and loss of an established composed cue are distinct cases. Do not waive steady
viewport/composition assertions to call this fixed. A subtitle-specific bar probe
is separate from physical AR detection; any substitute receipt must be an exact
current independently measured physical model, not retained shared geometry.

## Presentation handoff and consecutive analyzed-source reuse

The pending footprint verifier already rechecks current glyphs, backing/cleanup,
novel bar ink and bar planes. Its returned preview intentionally has no new bar
or detector authority. The tracker now accepts that per-call verified current
presentation through a separate held continuation path, retaining the original
accepted cue/ink reference and lease. It advances only a queue identity after
exact context, text, current ink and forward-source checks. Missing current proof,
changed ink, stale context or an unproved continuity gap still fails closed.

Footprint topology rearm supports fresh independently analyzed sources whose composition completed successfully, including sources after pending gaps. This is limited to the pending
footprint guard; ordinary cadence still requires stride 2 and its original spacing.
Rearm keeps the original native pixel baseline. A changed cleanup sample cannot
be absorbed as a new baseline simply because another semantic scan succeeded.
Changed glyph ownership or geometry must rebuild or reject. The native contracts
include a black-to-nonblack cleanup change followed by a pending rejection against
the original baseline, plus ordinary and queue-retired held continuation checks.

All 1,068 native contracts pass at clean 3d7fb6a8 (run035012-eed9d9c5). This is
native iteration evidence, not full runtime qualification or a measured renderer
speedup. The actual renderer run must count footprint rearm hits and repeated
builds, report costs including failures, and preserve strict established-cue
composition/frame-progression/framing checks before deployment is qualified.

The pending renderer now takes the exact previously completed final operation from
its current-source footprint receipt. It skips fresh layout/backing acquisition,
then consumes the existing one-use cleanup-protection receipt before hook binding.
This closes the later compositor bar-authority veto without populating bar fields.
A failed source/base/operation binding still routes current raw pixels.

Classic has a separate generic ready-source optimization. A fresh complete independent positive scan whose resolved result covers the accepted cue, or an exact current accepted-cue certificate for a pruned result, can rearm the same cue without rebuilding full-band directional arrays. It compares complete current raw
and owned masks, cue/layout/source/physical context, current glyph/fringe YUV and
backing state. The original pixel/contrast seed remains immutable; latest verified
source advances and pending age resets only for this new full positive. Duplicate,
held, pending, partial or context-changed results cannot take that shortcut.
These newer changes still require native and actual-renderer qualification.

The cleanup-projection hull uses only the prepared capture/content envelope. Raw
detector/result line boxes include analysis padding and are not shader sampling
support; including them wrongly treated changing current movie margins as glyph
changes. The UHD regression keeps those raw boxes wider than prepared capture,
requires the actual margin pixel to stay protected, and still rejects a change
inside the prepared operation. Glyph/core/fringe, backing and new bar-ink checks
remain unchanged. Ready-rearm first-rejection codes and layout/geometry submasks
are now recorded so any remaining expensive rebuild can be attributed before
changing an eligibility condition.

Tracking continuity is not itself a pixel-template change. After a genuinely new
complete independent positive source (including an exact current existing-cue
certificate when grouping prunes rows), the generic guard adopts the actual
current preview wrapper while keeping the original pixel seed identity, masks,
samples and contrast baseline separate. It rechecks all other physical/source,
policy, layout, mask and current glyph/backing conditions. Held, pending, duplicate
or foreign-context inputs cannot perform this rebase or renew its age. The
analyzed footprint cache likewise allows a continuity-only rearm only from a
successfully composed current source; ordinary cadence remains strict.

A separate PendingPreparedSubtitleErasureReceipt carries current display-only
composition proof through the final ownership stage. The renderer records it only
when current footprint verification passed, the exact operation's one-use cleanup
protection was applied, and this source's composition preparation succeeded. It
binds the source identity, ink, accepted cue, policy/continuity, physical/display
bounds and final operation. It may qualify prepared erasure for this source with
bar authority still absent; it cannot qualify semantic acquisition, native aspect,
absence, lease renewal or an older packet. Rejection controls cover stale/changed
identities, operation, mode, bounds, ink/cue and unapplied protection.

The renderer no longer invokes the previous-complete-output input-cadence repeat
as a response to optional subtitle readiness. A distinct input continues into
current-source preparation and raw fallback even when its analysis is UNKNOWN.
Explicit ingress cadence-repeat packets and display-refresh repeats retain their
own timing rules; missing optional work cannot select an older movie frame.

Runtime cadence tests enumerate their actual monitor, select an active 60 Hz
target, and supply the same target-refresh metadata as the GUI. They cover
59.94/60 and 23.976/24 Hz inputs. Every successful steady submission participates
in exact-source and wall-time joins, including raw fallback, UNKNOWN, genuine
repeats and stop-drained output. Bounds use source and actual display periods;
queue depths and analysis deadlines are unchanged. A 24 Hz monitor alone cannot
qualify 60 distinct source submissions per second.