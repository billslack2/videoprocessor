# Source-picture measurement and runtime ownership

The common spatial source measurement is the production geometry producer. Legacy
extraction remains an explicit reference/inspection API, not a source of production
geometry. Physical picture bounds, the retained presentation aperture, and permission
to discard source pixels are separate outputs. No screen ratio or fitting mode is an
input to measurement.

The sections describing the initial shadow prototype and its first replay below are
historical evidence. They do not describe the current production scheduling policy.
Current qualification must use the matching source and binaries; native capture replay
alone does not establish live transition or subtitle-rendering correctness.

The governing subtitle presentation contract is
[SUBTITLE_PICTURE_AR_CONTRACT_20261102.md](SUBTITLE_PICTURE_AR_CONTRACT_20261102.md).
Processed caption preservation must keep native movie dimensions; historical raw
visibility fallback rules do not authorize subtitle-driven resizing in processed modes.

## Measurement order

1. Validate the canonical native source and retain its source-coordinate domain.
2. Build bounded sampled topology with explicit sampling coverage. Unsampled pixels
   are not certified empty and this map grants no crop permission.
3. Retain competing local matte-to-picture departures on each edge. A gradual
   brightness drift is not an abrupt boundary; paired chroma cannot move a luma edge.
4. Identify repeated glyph rows separately from picture-edge support.
5. Reconcile candidate rectangles against clipped exterior occupancy. A connector
   into a panel cannot make its exterior header disappear. Contradiction alone does
   not establish another rectangle: canvas inference requires independent layout support.
6. Report measured coordinates, supported canvas inference with unresolved margins,
   partial evidence, or missing work separately. Ratios come from coordinates; no
   standard aspect or screen geometry is an input.

The topology overview is at most 480 by 270 sampled cells with at most 512 components.
Exhausting the component limit produces an incomplete result, never a trusted rectangle.
A sparse overview is not a guarantee that one-pixel objects are observed at every phase.
Exact discard verification remains a separate responsibility.

## Current runtime contract

The native worker owns geometry. Foreground consumption cannot run the old observer
when worker facts are missing. An exact completed result can be consumed after a
foreground deadline expires; that permission does not authorize new background work.
A normal geometry job has a 24 ms cooperative deadline. Expiration means unfinished
work, not evidence that bars are absent.

Completed geometry facts may be reused only after exact equality of every declared
native byte and compatible source interpretation, dimensions, pitches, format,
transport/format/domain identity and nondecreasing source sequence. These pure pixel
facts may survive a timeline discontinuity; continuity is enforced separately on
derived temporal, caption and publication proofs. The per-worker cache holds one owned
source of at most 64 MiB of declared storage, without copying its raster. An owner may
also retain other allocations; this is not a bound on that owner's total memory.
Unowned or oversized storage is not cached. No crop certificate, glyph mask, subtitle
lease or temporal vote is reused through this cache. Current crop verification runs
against the current source even when geometry is reused.

Current measurement publication is monotonic within a source: a first completion may
replace unfinished work, but an older source or another interpretation cannot overwrite
completed current geometry. Completed negative verification is distinct from unfinished
verification. Retention changes recheck excluded bands using the same source facts;
they do not invoke another geometry detector. Exact spatial subtitle recovery consumes
the current identity, measured nominee and verified retained bounds, not a legacy fixed
padding requirement.

## Current-source preparation without lookahead

Source preparation includes the already accepted head even when temporal AR
lookahead is zero. Ingress dispatches metadata-only work using owned canonical
storage; raw Dolby tunnel bytes are not treated as converted pixels. With zero
lookahead, subtitle work is admitted after this source's native measurement completes,
including a completed negative. Already queued future sources retain independent
parallel glyph preparation; exact native/caption proofs are joined at consumption.

At zero lookahead, the accepted head has a cooperative preparation deadline of
one input interval from its original enqueue timestamp, capped at 50 ms. This can
add source-to-display latency; it is not a zero-wait promise. Completed native and
due subtitle work permits presentation immediately. Disabled or cadence-skipped
subtitle work imposes no additional wait. The stage does not wait for trusted
geometry, a desired aspect, detected captions, future frames or confirmation votes.

Completion notifications wake the preparation stage. Each attempt uses the same
absolute deadline; retries cannot extend it. Expiry, ownership/context changes or
contended stage locks leave missing-work semantics intact. Existing queue capacity
and overflow policy remain unchanged, and capture never waits for analysis. This
is a cooperative CPU/OS scheduling bound, not a hard realtime guarantee. Stage
telemetry records the source, completion state, elapsed time, interval and exit
reason. Crop/caption permission still requires the exact source's verified proof.

## Native geometry and output visibility

An accepted native picture rectangle is not permission to hide untranslated raw
captions. Advanced subtitle modes may present the native rectangle when exact
current composition is qualified, when current caption absence is confirmed with
no contradictory current raw-visibility expansion, or during the explicitly
qualified initial shader-building interval. Requested mode alone grants no crop
permission; completed shader programs do not make a missing cue ready.

Other unprepared outputs preserve the exact current raw-visibility envelope, or
the full raster if that evidence is unavailable. This changes the presentation
aperture only: measured and accepted native picture bounds remain unchanged.
Optional fill cannot trim this visibility fallback. Fresh qualified current
composition restores native presentation immediately; one late scan must not
latch a wider viewport until the cue ends. Stable prepared playback must still
preserve its viewport and complete captions through natural skipped analyses.

## Completion and temporal ownership

Both raw worker publishers use the same monotonic publication predicate. Missing
work may complete; a completed geometry result cannot be replaced by another
nominee or a late timeout. Exact source identity and owned storage are checked
before that predicate. Retention keys include the immutable raw-result revision,
so a result derived from pending work cannot impersonate a later completion.
Completed negative geometry is final; genuinely unfinished work remains eligible
for another job only under the existing dispatch budget and queue rules.

An unfinished source is not registered as completed negative lookahead evidence.
Late current-caption verification is a different case: the native geometry can
already be complete while an unmasked crop is provisional. A typed completion may
add current crop permission only when its private current-source proof matches the
original spatial nominee and exact identity. It does not replace measured geometry.
For the latest unconsumed provisional observation, one bounded pre-observation model
checkpoint permits replacing that evaluation once. It adds no second confirmation
vote and preserves the original presentation intent. Newer model observations,
published decisions, consumption and context/policy/continuity changes invalidate
the checkpoint. Older pending completions may update the exact-source evidence
ledger but cannot rewind newer temporal state.

## Initial shadow prototype (historical)

`VP_SOURCE_PICTURE_SHADOW=1` enables diagnostic evaluation on the existing native
analysis worker. It defaults off. A shared admission policy allows at most one new
measurement per 500 ms, with a 12 ms cooperative deadline. No additional work queue,
full-raster copy, render-thread scan, or crop/AR publication is introduced. Facts live
inside the existing exact-source worker result and emit `source-picture-shadow` records.
The limits bound optional work; expiration is missing evidence, not a negative result.
The observation's retained topology is at most 259,200 label bytes, plus components
and edge candidates. Temporary CPU scratch is released with the evaluation.

The separate production timing repair consumes already-ready exact source evidence
when the optional fresh-scan deadline is zero or expires. The new expired-ticket fallback cannot submit background jobs. A fresh original
zero-foreground-budget pass retains its existing bounded asynchronous dispatch
opportunity after missed priming; it still performs no foreground pixel scans.
Foreground evaluation, background dispatch, and consumption of ready evidence
have separate permissions. Repeated displayed inputs do not gain
new votes. Existing source identity, pending-prefix, epoch and effective-source checks
still govern publication. No new confirmation dwell is added.

## Competing boundaries and enclosing-rectangle reconciliation

The current qualification predicate is independent for each side. Candidate lists
are sorted outward first (ascending left/top, descending right/bottom), including
after fixed-span candidate merging. Glyph classification changes eligibility,
not order. A qualified raster-terminal fast path returns the outermost possible
coordinate; dark-terminal completion inserts that same extreme only when the
other three selected boundaries are raster terminals. Consequently, the first
eligible boundary on each side forms a rectangle containing every rectangle made
from the remaining eligible boundaries. This is dominance pruning of nested
alternatives, not a claim that only one source edge exists.

Positive exterior-picture evidence contradicting that enclosing rectangle also
contradicts every nested alternative: those alternatives still exclude the same
observed pixels. Do not retry deeper rectangles and clear this contradiction when
their exterior component count decreases. Clipping farther inward can connect
previously distinct exterior header components through an excluded panel, so
the count-based reconciliation predicate itself is not monotone. The focused
`SourcePictureOuterContradictionDominatesCompetingInnerEdges` fixture covers this
case with two independently qualified top edges. This argument depends on the
current side-local qualification and outward ordering; pair-dependent edge
qualification or non-nested hypotheses would require a different reconciliation
search. No contradiction is not dense absence proof or permission to crop.

Candidate generation and topology are bounded work. Cancellation, source-read
failure, or the topology limit leaves the observation incomplete and cannot
publish a measured rectangle. Missing eligible edges remain partial evidence;
they are not negative picture evidence. Existing glyph, canvas-scaffold and
current-source crop-verification rules remain necessary independently.

Each edge also retains its 128 supporting native ray positions (16 bytes per
candidate), including merged coordinate uncertainty. Reconciliation rejects a
nominal rectangle when both selected perpendicular boundaries share distributed
support outside it: at least the existing quarter-of-probes support requirement,
spread over three quarters of the excluded span. This adds no pixel reads and
does not lower the overview foreground threshold. It catches a dark picture strip
whose native boundary departure is measurable even when coarse foreground
topology does not classify that strip as occupied. The result is partial geometry,
not a forced raster contact or crop permission. Narrower or sparsely supported
ambiguities remain outside this positive contradiction test's support scale.

## Validation and migration gate

All new functional and conversion contracts are registered in the mandatory native
gate. `SourcePictureOptionalNativeCaptureReplay` additionally accepts private captures
through `VP_SOURCE_PICTURE_CAPTURE_DIRECTORY`, `VP_SOURCE_PICTURE_CAPTURE_EXPECTED_EDGES`
(left,top,right,bottom), and `VP_SOURCE_PICTURE_CAPTURE_EXPECTED_OUTCOME`. An absent
private directory explicitly records zero capture coverage; it is not a corpus pass.
Do not commit captured movie pixels.

The runtime authority contract carries measured geometry and uncertainty separately
from the conservative retained aperture and permission to discard pixels. Overwriting
`MakeActivePictureObservation.bounds` while retaining its trusted classification would
silently grant crop authority and is prohibited. Preserve current subtitle proof bases.
Consolidate source events and commit ownership before removing legacy inference routes.
Final acceptance still needs full-rate scope/IMAX transition capture and live playback;
passing paused native frames is not proof of those transitions.

The first optimized private replay exercised 479 saved frame instances in seven capture
sets with zero mismatches (including 15 byte-identical paused scope instances). Average
CPU measurement time was 5.5â€“9.4 ms by set; observed peaks reached 14.1 ms. These are
unbudgeted CPU replay timings, not live latency guarantees. The 12 ms opt-in deadline
may intentionally leave slow packets incomplete; its admission rate stays two per second.
These historical timings are not qualification of the current runtime integration.
No private pixels are included in this repository.


### Ready evidence during backlog

Foreground pixel evaluation, bounded background dispatch, and consuming completed
source facts have separate permissions. A compatible fresh queued source may
consume its exact ready prefix during backlog without a new scan or submission.
Repeat, discontinuity, resize and output renegotiation exclusions remain; full
source/domain/owner, policy/continuity and pending-prefix checks still apply.
Original zero-ticket background dispatch keeps its earlier non-backlog admission;
an expired positive-ticket fallback cannot create additional background work.
This removes avoidable loss of already-ready native evidence. It does not make
late subtitle scans ready, extend complete-image repeat grants, or renew cue leases.

### Accepted subtitle proof handoff

A three-line cue already accepted from four consecutive complete raw observations
may export the existing current-source seed certificate even when the caller's
short lookahead grouping omitted one row. The certificate retains exact current
identity, immutable ink ownership, independent bar evidence and full accepted
geometry. Held, revised, partial, weak and unresolved candidates cannot mint it.
Pending recovery still validates current pixels and the original age limit; queue
retirement or replaying the certificate cannot renew the lease. This changes no
native AR vote, repeat allowance or shader-building exception.

The renderer emits a computed-only `subtitle-pending-seed` diagnostic disposition
through the existing trace, without extra pixel scans or persistent logging. A
failed actual-renderer test prints at most 32 matching source/context dispositions
so a missing seed can be distinguished from rejected current pixels or expiration.
