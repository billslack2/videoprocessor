# Source-boundary review, 2026-10-31

Five independent Luna xhigh research passes reviewed spatial detection, lookahead,
known/remembered ratios, adversarial scenes, and integration with the existing
source-evidence pipeline. This document records the design decision, not a claim
that a new runtime has passed full qualification or manual playback.

## Decision

Evolve the existing source-pixel measurement and immutable source-evidence packet.
Use spatial shape and contrast relative to a locally measured matte. A small
amount of noise, chroma variation, a raised black floor, or localized subtitle/UI
pixels must not invalidate an otherwise well-supported border. Apply the same
principle to SDR, HDR and decoded Dolby Vision; no DV-only exemption is warranted.

The working observer fits each column's reference from three source rows and
checks two additional rows. It masks contaminated columns and reuses the mask
through coarse nomination, refinement and inward continuity checks. Acceptance
requires broad separated support. A raster-sized nomination remains a search
hint: it must not prevent finding encoded bars inside the input raster.

A brighter interior gradient is not the picture boundary. The first sustained
source departure must be preserved; a faint coherent collar or competing border
can require uncertainty. Short detached bar graphics must not become boundaries.
The measured physical rectangle and conservative pixel-preservation envelope
remain distinct. An observer estimate does not independently authorize a crop.
The optional physical observer retains its existing two-millisecond budget.

## Dynamic transitions and useful priors

Already available lookahead can corroborate a change without another scan timer
or fixed real-time wait. Existing queued depths are three frames through 30 Hz
and five above 30 Hz; these are capacity limits, not a three-of-five vote or a
mandatory delay. Current exact source identities, generation and continuity must
match. Rewind, seek and source replacement invalidate incompatible evidence.
The 24 distinct displayed frames in renderer tests are an observation window,
never a runtime transition requirement.

Known ratios and remembered exact rectangles should rank candidate boundaries
and focus pixel work. They do not replace source evidence or round a measurement
to a screen/standard ratio. A current visible edge can anchor a candidate for an
occluded opposite edge; current pixel checks must still validate its proposed
extent and reject contradictions. Broad real picture outside the old crop can
withdraw old authority promptly even before every new boundary is exact.

Existing remembered-family code retains exact measured representatives, learns
from qualified native geometry across scenes, and checks current pixels on recall.
Its three-family capacity and common-ratio learning eligibility are limitations
identified by the review. Expanding those is a follow-up experiment with explicit
nonstandard-ratio, stale-history and competing-edge tests, not part of this
observer patch. Caption geometry must never train picture history.

Above 30 Hz, an unavailable analysis input may retain one previously submitted
whole image, with the original displayed identity, geometry, caption and expiry.
It cannot manufacture new measurement votes or renew a cue. Shader warmup is a
separate startup state: the operator permits caption clipping while compilation
is pending. Any warmup-image reuse must preserve that original Pending provenance
and must not weaken post-composition checks.

## External comparisons

[FFmpeg cropdetect](https://ffmpeg.org/doxygen/8.0/vf__cropdetect_8c_source.html)
provides scanline aggregation, outlier handling and an alternative edge/motion
mode. These are useful comparison points; HDMI captures do not provide decoder
motion-vector side data. Its output rounding or temporal defaults are not VP
requirements.

[Hyperion's detector](https://raw.githubusercontent.com/hyperion-project/hyperion.ng/master/include/blackborder/BlackBorderDetector.h)
uses multiple scanlines and includes an OSD-oriented mode. Distributed sampling
is relevant, but a lighting-oriented symmetric border result cannot substitute
for VP's independently measured source coordinates and picture-preservation proof.

Our inference from these comparisons and the existing VP architecture is that a
bounded spatial estimator with robust reference strips, explicit occlusion masks,
source-bound lookahead and verified candidate priors is a better fit than importing
a generic crop filter or adding a long temporal majority gate.

## Required evidence

Keep failing-first tests for noisy/raised/tinted matte across native formats and
ranges; localized captions/UI; raster-hint bar acquisition; faint picture collars;
short detached graphics; asymmetric, pillarbox and windowbox geometry; exact
nonstandard ratios; source resets; and bounded cancellation. Replay supplied
native captures separately from synthetic controls and report actual frame counts.

Actual renderer checks must cover both shipped dependencies, 23.976/24 and
59.94/60 input cadence where applicable, source ownership, original whole-image
repeat and fresh successor, native-boundary independence from captions, and
transition output geometry. Warm-cache iteration does not replace cold shader
coverage. Partial evidence cannot issue a full runtime qualification receipt.

## Native validation checkpoint, 2026-11-01

All 837 mandatory native methods pass in run 20261031-235938-d9be7b97,
including all 64 earlier native captured frames. That set measures source rows
263..1897 at 3840x2160 (2.3500611995:1), retaining the distinct 254..1906 guard.
All 56 newer paused-capture frames also pass in 20261101-000123-67b138bc:
275..1884 (2.3865755127:1), retaining their recorded 274..1884 guard. No standard
ratio, screen aspect or symmetric rounding supplied either measurement.

Observer-only replay cost, excluding file I/O, averaged 0.3431 ms (peak 0.5358)
for the 64-frame set and 0.3096 ms (peak 0.5288) for the 56-frame set. These are
controlled replay measurements, not a guarantee of live scheduling latency.
The optional two-millisecond cancellation budget remains unchanged.

The supplied 95-frame V210 full-raster set has a separate dim-edge uncertainty.
Its left-edge profile is spatially structured but does not meet the current
terminal brightness margin. A texture/coherence fallback needs adversarial
raised and asymmetric matte tests before implementation; this patch does not
claim that case resolved. The source capture also postdates the screenshot and
must not be used as pixel proof of that earlier instant.

Actual renderer qualification, cold shader preparation and dual-dependency GPU
checks remain separate from this native checkpoint. No new live playback or
full qualification is claimed by these results.

## Owned lookahead and runtime validation follow-up

The full a2326513 run built all six matched Release projects, then passed
848/851 Standard tests; Beta was not reached. One failure was fixture provenance
leaking across reused renderer addresses. Two exposed startup gaps where queue
retirement or picture-snapshot rebinding discarded useful exact-source subtitle
scans and future confirmation before the first cue existed.

The repair retains only producer-owned prepared scans and recomputes confirmation
from current and following pixels. A typed timeline witness allows complete exact
scans across verified retirement of older queue entries; it clears obsolete AR
proof and never renews caption expiry or restores native crop authority. All843
native methods passed before the storage refactor, including64/64 captured frames.

The first six-case renderer run aborted with stack-overflow exit0xC00000FD and
noTRX. New nested buffers have been moved to heap-owned workspaces and an explicit
output-reference resolver. Measured object sizes are6992bytes per observation,
60512bytes per preview and55936bytes per eight-observation array. No matching
crash dump exists, so the exact faulting call remains unconfirmed. Five focused
native controls pass after this refactor, including the production heap path.

A new mandatory test also proves that a composed-output flag alone can admit a
partial two-line caption. Persistent three-line renderer fixtures now require a
matching preceding subtitle-output record with all three raw and accepted lines.
Repeated images must use their original selected source's proof. The manifest now
requires844native methods. Actual renderer, full cold-cache and dual GPU/cue
validation are still pending for this candidate; no deployment is claimed here.

The separate dim-full-raster experiment measured three of95V210 frames. The
proposed local texture continuation supplied zero extra votes, and its broad
inward-departure veto fired on real picture detail. That heuristic was rejected
without implementation; the95-frame uncertainty and visual transition flash
remain open.

## Completed-result harvest checkpoint

All845mandatory native methods pass in20261101-011054-8bc2db41. The new worker/
resolver control reproduces an early two-observation prefix, then reads a third
completed source and combines it with the actual raw predecessor to preserve all
three cue lines. It submits no new work, retires no source and keeps the existing
short-window false-positive checks. Missing, partial, foreign and changed-context
futures cannot manufacture confirmation; a shorter late read cannot erase a
stronger still-owned prefix.

The renderer now performs this completed-results-only harvest after native
lookahead and before dequeue, while the original source snapshots remain owned.
It uses the same producer and final ownership checks, without an extra wait,
conversion, pixel scan, timer or cue-expiry extension.

The corrected actual oracle uses harness callback order and the selected input's
timeline context, matching fields that the diagnostic producer actually emits.
Against the unchanged42ce1661 runtime it passes3/6 focused cases and reproduces
one partial-caption output plus two startup gaps. Actual validation of the late
harvest remains next; no new deployment or full qualification is claimed.

## Bounded raw subtitle history checkpoint

The completed-result harvest at d4e08b4c passes four of six focused actual
renderer cases (20261101-011239-342375d3). The two remaining failures expose
complete current raw scans with an insufficient ready future horizon. Older
independently scanned sources already existed, but only one predecessor was
retained for crowded three-line confirmation.

The presentation now retains at most three original raw predecessors and combines
them with current and already-ready future scans. The existing four-distinct-source
pixel, glyph and backing checks remain mandatory. Only current geometry is
promoted; history never supplies output bounds, native AR authority or a renewed
cue lease. This adds no runtime wait or analysis submission. Unknown inputs,
pending work, gaps, context changes and changed same-source interpretations clear
the chain; repeated images and duplicate calls add no votes.

Both short-horizon regressions failed before implementation. All 848 native
methods then passed in 20261101-012259-b10277a7. Independent review found an
omitted duplicate-backing comparison; expanded controls reproduced that failure
in 20261101-012857-8fdf0069 and pass after repair in
20261101-012938-d81cd4af. The duplicate test covers 17 lifecycle cases, including
backing-edge coordinates/support and measured/proven panel metadata. Actual
renderer and full cold-cache validation of this final candidate remain next.

## Worker scheduling and changed-plane retry checkpoint

The ceb7e211 matched Release run passes five of six actual renderer cases
(20261101-013042-c237da79). The remaining Beta Generated failure follows queue
recovery into an unfinished source. Its trace also shows two concurrent scans of
the same source after native geometry becomes available, consuming both worker
slots. Changing queue recovery or extending subtitle expiry is not justified.

Pending and active raw scans now coalesce an exact source/settings/policy/
continuity request when its picture plane becomes available. The original job
key and closure stay immutable. A completed raw result may satisfy that new plane
only with conclusive independent bars, complete searches and immutable ink.
Complete caption absence is valid evidence; incomplete, work-limited or dependent
results cause a miss so the canonical scan can run. Same-key partial observations
remain observable. No new wait, source vote or AR authority is introduced.

Valid-domain dispatch regressions fail with the old two-job behavior in
20261101-014136-4cbd3872. The wider run then exposed incomplete-result retry
suppression, reproduced in 20261101-014225-a818df00. All 850 mandatory native
methods pass after both repairs in 20261101-014730-4c362151, including 27 work-key
mismatch controls and a 15-case complete/incomplete-result matrix. The legacy
stale-plane test still rejects dependent results and verifies that distinct
available planes execute separately. Actual renderer and cold qualification of
this final worker change remain pending at this checkpoint.

## Cold first-Ready acquisition and test-iteration checkpoint

The clean d57a5ec9 candidate passed all six actual renderer repair cases and
217 GPU/cue tests on each dependency. Full fresh-cache qualification then passed
863/864 Standard methods; the Unknown-input case exposed a distinct first-Ready
failure: source 29 had a complete raw three-line scan but lacked four-source cue
acquisition. Raw analysis completion was incorrectly treated as display readiness.

The existing single-input warmup bridge now tests cue acquisition on a heap copy
of the tracker before mutating live presentation. It retains an independently
measured consumed input in RAW history only after successful original-image
repeat; the repeated image grants no vote, cue renewal or native AR authority.
An exact-current proven native AR change vetoes this new fallback. No warmup
lease extension, four-source threshold relaxation or new runtime wait is added.

The acquisition and handoff regressions fail before implementation in
20261101-015559-298f571d. All 854 native contracts pass after implementation in
20261101-015836-746d5486. Actual renderer/cold qualification remains pending.

The broad GPU baseline took approximately 24 minutes per dependency. Correctness
fixtures now share bounded compiled-program bytes across otherwise fresh devices,
renderers and resources. Dedicated cold-cache MotionRendererTests remain isolated.
A new GPU regression checks program reuse and actual composition pixels. This
speed change is implemented but not yet built or benchmarked at this checkpoint.
