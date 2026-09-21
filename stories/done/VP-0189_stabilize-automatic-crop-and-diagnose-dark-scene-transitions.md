# VP-0189: Stabilize automatic crop and diagnose dark-scene transitions

## Status

Done (2026-09-20). Accepted and closed by explicit user decision. Historical implementation, validation, and any previously recorded limitations are retained below; this status change does not claim new testing.

## Historical status and evidence

Review

Merged into `v1.3.005-beta` through [PR #94](https://github.com/billslack2/videoprocessor/pull/94)
on 2026-09-17 EDT (2026-09-18 UTC), merge commit `cb1808578f7d34b675a82a9802c8c0b6e39a315f`.
**Keep this story in Review per the user's explicit instruction after merge.**
The user reports everything watched on deployed `525e5802` looked good.
The merged source tree exactly matches that tested build. See the final
integration section below; it supersedes earlier interim deployment and timer notes.

Follow-up [PR #95](https://github.com/billslack2/videoprocessor/pull/95) merged
on 2026-09-19 into `v1.3.005-beta` at `8414b11633d3ab8cc06fa3964c273c81b76f05bc`.
Its source tree is identical to tested/deployed `6dc92012`. Status remains
**Review**, explicitly requested again after this merge; see the September 19
follow-up section for current validation and remaining field coverage.

Created 2026-09-16 at the user's request after source review and standalone
policy experiments. Implementation started from the explicitly requested latest local tip. Additional crop diagnostics
are a required part of the design and acceptance, not an optional follow-up.
The original viewing-session log was received and independently reviewed during
implementation. Star-field logs/video remain unavailable. See the received-log
review below for verified counts and corrections to the supplied report.

## User story

As an operator watching films with changing aspect ratios, fades, dark action
and star fields, I want automatic crop to remain stable through uncertain
measurements, reveal genuine picture outside the crop, and recover promptly
when bars return. When it changes or stays released, I want the log to explain
the evidence and decision well enough to diagnose it without guessing.

## Evidence and historical baseline

- The user reported repeated short crop losses around fades/dark moments,
  one nearly minute-long release, rapid zoom-in/out during a dark action scene,
  and additional problems with star fields in multi-AR content.
- The supplied `2026-09-16_crop-geometry-report.md.txt` reports 79 applied-state
  changes in 31 seconds and a 54-second release on `94f938d3`. The raw log subsequently confirms the counts but contradicts the claimed
  single-latch cause of the 54-second interval. Actual excluded-band pixel
  contents remain unverified; see the received-log review.
- GitHub beta `v1.3.005-beta` was verified at
  `94f938d33216d0f212797f1ed4c6f83db02d9187` on 2026-09-16. Crop policy and
  active-picture evidence are byte-identical in VP-0188's `3acd02b3` follow-up.
  The mechanisms below predate both drops; do not describe them as a new
  regression introduced by the configuration change or REQ006 integration.
- A compiled, unchanged-beta policy probe reproduced 23 applied-state changes
  in 24 alternating-safe/unsafe synthetic frames without scene cuts. Evaluated
  frame-local retention bypasses the legacy ambiguity/scene holds.
- After one outward sample, 1,440 safe samples with observed bounds
  `0,44-3840,2116` never advanced recovery against saved `0,40-3840,2116`.
  Returning the observed top to 40 recovered in seven qualifying samples at
  23.976 fps. Exact observation equality rejects a harmless contained inset.
- Ten qualifying outward samples set `confirmedNonNearBlackContent` at
  23.976 fps. The probe then remained full raster through 1,440 exact safe
  samples. The sticky flag disables ordinary recovery after outward content ends.
- Correct the attachment's explanation: recovery already exists. Restrictive
  equality and disabled recovery are the issues, not a wholly absent return path.
- Sparse moving stars can plausibly alternate hits/misses on fixed sample grids.
  This remains a source-based hypothesis, not a reproduced star-field incident.
  Global P90 darkness does not establish whether outside pixels are real stars.

## Scope and design

1. Separate evidence that cannot affirm geometry from positive evidence of
   visible pixels outside the current crop. `retention_safe=false` currently
   combines excluded-band safety with proposal containment and must not be
   interpreted as a single kind of conflict.
2. Give ambiguous final presentation changes explicit temporal state. An
   unresolved sequence must not alternate crop/full on every safe/unsafe sample.
   Preserve the previously trusted crop only under a bounded inspection/hold
   contract or positive current safety. Never expire continuously positive
   safety merely because a timer elapsed.
3. Use asymmetric confirmation around an outward conflict: reveal credible
   outside picture promptly, then require sustained affirmative bar evidence
   before returning inward. A lone subsequent safe sample must not rearm the
   same unresolved conflict. Keep already-trusted full-raster authority immediate.
   Preserve bounded recovery so this does not create another indefinite latch.
4. Repair return to the saved crop when a current observation is safely
   contained inside it. Retain the exact saved trusted contract, compatible
   source/raster/presentation provenance, and fresh excluded-band safety checks.
   This restores a known crop; it must not grant new or deeper crop authority
   from an arbitrary dark subrectangle.
5. Revalidate full-raster presentation after temporary outward content ends,
   including a previously sticky episode. Distinguish established full-raster
   authority from a temporary presentation fallback. Do not require a scene cut
   to return to a freshly proved safe crop; do not release simply because global
   P90 becomes brighter or an elapsed timeout expires.
6. Count independent evidence by decoded source sequence. Cadence repeats,
   stale/out-of-order evidence, or changes of source/raster/presentation epoch
   cannot incorrectly advance or carry confirmations. Preserve deliberately
   supported paused-frame behavior without treating repeats as fresh samples.
7. Preserve subtitle translation/Fit ownership, explicit fail-open, generation
   invalidation, and full-raster visibility rules. Coordinate at final crop
   arbitration so competing owners cannot bypass stabilization. Keep logical
   aspect authority separate from applied crop, translation, Fit and NLS mapping.
8. Keep fixes local to unresolved presentation/recovery. Do not add a blanket
   multi-second inward dwell or change black/near-black thresholds to treat
   stars as noise. Select and document confirmation counts/durations and maximum
   ambiguous hold/recovery budgets with measured frame-rate coverage before
   implementation is considered ready. Genuine confirmed aspect transitions
   and stable-bar acquisition must retain their existing timing.

## Required diagnostic design

Use the existing logging path and stable, machine-readable key/value records.
Extend existing crop/episode records where sufficient; avoid duplicate schemas.

| Record | Required information |
| --- | --- |
| Applied presentation change | Source sequence and generation, measurement sequence, presentation/profile epoch, scene boundary identity, cadence repeat; previous/new applied state and source bounds; saved trusted and observed bounds/classifications; previous/new owner; stable reason code and readable reason; logical geometry versus actual presentation change. |
| Hold or recovery state change | Hold/re-entry/episode identifier and state; entry crop and full-raster start sequence; confirmation count/required count, elapsed/required dwell and units; sticky state and why it was set/cleared; start, qualify, reset, expiry and release reasons. |
| Evidence rejection | Separately report analysis validity, evidence freshness, excluded-band safety, proposal availability/containment, global near-black/P90, current full-raster authority, outward visibility and affected edges. Recovery must report the failed gate(s): unavailable/stale evidence, observed-rectangle mismatch or noncontainment, trusted-contract mismatch, source/epoch mismatch, cadence repeat, unsafe band, sticky/full-raster ownership, or interrupted confirmation. |
| Bounded detailed edge evidence | Reuse already-computed per-edge black fraction, P90/texture/continuity, measured outward extent and available spatial-support/sample counts; identify unavailable fields explicitly. Include the relevant thresholds/sampling configuration once per diagnostic session/change. Do not label an object as a star or subtitle solely from sparse samples. |
| Unresolved-event summary | Event duration, applied transition count, evidence flip/reset counts, last blocking gate(s), first/last source sequence and final disposition. Enough context must remain to explain a long release even if proof stayed at zero throughout. |

- Normal logging records meaningful applied/owner/hold/recovery transitions and
  rate-limited summaries of persistent unresolved states. Ordinary stable
  playback must not generate per-frame crop logs or logs for every raw edge jitter.
- Additional per-sample detail must have an explicit opt-in, bounded duration/
  record budget and documented invocation. Reuse the existing verbosity/control
  model if possible; do not invent a permanent always-on high-volume stream.
- Do not count cadence repeats as new evidence or new applied-state changes.
  Logging must not run extra image scans, stall presentation, or change decisions.
  Reuse existing timing telemetry to quantify enabled/disabled overhead.
- Add a small log-analysis helper and schema/example documentation. It must
  select an interval, count actual applied-state changes using exact field names
  (`applied` must not match `shift_applied`), group by event/source/scene, summarize
  recovery rejection reasons and report insufficient evidence explicitly.
- Document that existing `capture_missed` counts inferred timestamp-gap frames,
  separately from renderer queue drops. It is not independent proof of delivery
  loss and must not be used to explain crop changes without correlated evidence.
  Counter arithmetic and a wider capture-telemetry redesign are outside this story.

## Acceptance criteria

1. Reproduce the baseline policy failures as automated tests before changing
   behavior. An alternating ambiguous safety sequence with no new affirmative
   authority cannot cause repeated crop/full oscillation: at most one fallback
   within that unresolved event, followed by stable presentation until evidence
   satisfies the documented return contract. A later genuine AR transition
   remains allowed even if no scene cut occurs.
2. One outward transient followed by safely contained current observations
   restores the saved crop after the documented bounded proof. Exact agreement
   remains supported. The `top=44` versus saved `top=40` case cannot stay at zero
   proof indefinitely when all other safety/provenance gates qualify.
3. Sustained outward content followed by fresh sustained bar evidence can
   recover within the documented bound without a scene/source change. Continued
   outside picture and established full-raster authority remain fully visible.
4. Pixel-level fixtures cover moving sparse stars inside stable letterbox
   content, a real expansion with stars in the formerly excluded bands, sparse
   full-height content, return to letterbox, fades and subtitles/overlays across
   these cases. Vary point positions relative to sample grids. Confirm actual
   resulting bounds against fixture truth as well as policy flags. Record any
   remaining detector sampling limitation explicitly; do not claim all star-field
   recognition is solved by a temporal policy test.
5. Stable bars, ordinary confirmed full-raster/AR transitions, and subtitle
   translation/Fit retain baseline geometry and source-sequence timing. Any
   intentional delay is confined to ambiguous fallback recovery, documented and
   bounded. Do not revive VP-0136's rejected broad eight-second inward dwell.
6. Tests cover 23.976/24 and 59.94/60 fps, source-sequence gaps/repeats, pauses,
   scene boundaries and source/raster/profile epoch changes. Stale proof cannot
   hide current pixels or restore a crop from another context.
7. From a produced log alone, the analysis helper can distinguish uncertain
   proposal from measured outside-band content, held crop from timed hold,
   sticky release from geometry/provenance rejection, and applied change from
   logical-only/overlay changes. Test reset reasons and prolonged zero-proof
   states, missing detail, exact field parsing and cadence deduplication.
8. Logging enabled versus disabled produces identical crop decisions. Verify
   steady-playback log volume, bounded diagnostic capture and presentation cost.
   Run appropriate focused and relevant core tests plus a successful x64 Release
   build; record exact commands/results and any existing unrelated failures.
9. Obtain user validation on repeatable dark/multi-AR/star-field passages with
   the new diagnostics. Record title, approximate playback time, selected
   settings, observed symptom and log interval. Keep original report counts
   separate from independently measured results. Preserve correct full-height
   passages and credits. Missing original logs do not block implementation, but
   do block claiming the reported session was replayed or visually accepted.

## Implementation sequence and readiness

1. Rediscover the current beta integration branch on `billslack2/videoprocessor`,
   fetch its tip and use a clean source worktree under `E:\codex\videoprocessor`.
   The historical hashes above are evidence, not a future integration baseline.
2. Reconcile current crop, episode, subtitle and look-ahead ownership; specify
   the finite-state transitions and bounded confirmation/reset contracts.
   Check overlaps below and record the readiness review before source work.
3. Build failing regression fixtures and diagnostic assertions, then implement
   the narrow crop/recovery changes and associated logging together.
4. Validate composed renderer wiring and the log helper, run Release checks and
   move to Review with exact residual hardware/visual acceptance requirements.
   Deployment remains a separate requested action, using a matching tested x64
   Release host/plugin pair and preserved user configuration.

No child stories are assigned initially; this is one coherent presentation
stability change with required diagnostics and independently checkable criteria.

## Related work and boundaries

- VP-0163 (Done) established one-edge inspection/overlay retention and near-black
  episode behavior. This story covers the remaining generic ambiguity and
  recovery paths; preserve its accepted subtitle/scrolling-title protections.
- VP-0136 (In Progress) concerns false publication of a new same-axis logical
  aspect. Its broad eight-second fix was reverted for delaying real AR changes.
  Coordinate tests and ownership; do not silently subsume or close that story.
- VP-0124 concerns outward logical look-ahead. Preserve its current-frame and
  source-sequence authority contracts when final presentation is stabilized.
- VP-0188 configuration reload behavior, capture recovery under VP-0115,
  REQ006/color/LUT/gamma work, fixed-aspect crop semantics and OCR are outside
  this change. No active configuration replacement or detector threshold retune
  is authorized by the incident document.

## References and creation audit

- Immutable baseline policy: [AlphaSourceCropPolicy.cpp](https://github.com/billslack2/videoprocessor/blob/94f938d33216d0f212797f1ed4c6f83db02d9187/src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.cpp)
  (episode release 1624; sticky/recovery 1646-1755; final crop 1938-2034).
- Immutable evidence: [ActivePictureEvidence.cpp](https://github.com/billslack2/videoprocessor/blob/94f938d33216d0f212797f1ed4c6f83db02d9187/src/VideoProcessor-Lib/ActivePictureEvidence.cpp)
  (sampling 86/316/643; retention 687-815).
- Renderer wiring: `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
  (baseline 8284-8346, 8552-8577, 10296-10438, 10447 onward).
- Existing regression file: `src/VideoProcessor-Test/AlphaSourceCropPolicyTests.cpp`
  (star-field handoff 1313; timer/safety 3200/3231; episode recovery 4188/4281).
- Baseline reproduction sources and output: [VP-0189 assets](../assets/VP-0189/README.md).
- Local full review: `C:\Users\bslac\Documents\ChatGPT\Done\crop-review-20260916\review.md`.
  Supplied report: `C:\Users\bslac\Downloads\2026-09-16_crop-geometry-report.md.txt`.
- Creation audit against fetched `origin/main`: 207 canonical files and index
  rows; root maximum VP-0188; no duplicate/missing IDs, child-sequence problems
  or registry/count conflicts. Assign VP-0189; resulting count 208, next VP-0190.
  Pre-existing VP-0088 writes `In progress` rather than `In Progress`; its folder
  and index state agree. Record this capitalization discrepancy and leave the
  unrelated story unchanged.
## Tester coordination (2026-09-16)

The confirmed source/policy reproductions are sufficient to progress into the
implementation readiness review without the original session log. The next
source work should establish bounded state/reset contracts, add regression
fixtures and diagnostics, then implement and validate the narrow corrections.
Do not wait for field logs to start those steps; use returned logs to correlate
actual incidents and validate star-field behavior rather than guess thresholds.

A [ready-to-forward tester brief](../assets/VP-0189/tester-brief.md) asks for
existing complete raw/rotated logs, build/settings, title/playback and wall-clock
time, subtitle/overlay state, the exact symptom and a repeatable passage. It
also describes comparison testing once a candidate is available. No message
has been sent to a tester by this task.

Before sharing a candidate binary, complete focused/composed regressions, the
relevant core checks and a successful x64 Release build, then package the
matching host/plugin pair with exact build identity, the diagnostic invocation
and the log helper. Do not label the source probe as a tester build or claim
visual/field acceptance before the tester returns evidence. Implementation followed that coordination update; see the current Status.

## Implementation readiness and start (2026-09-16)

The user explicitly requested implementation from the newest local source commit,
even if unmerged. Verified local source branch codex/vp0188-current-beta at
3acd02b3c597896beea1eca72fc4a3730cd6d92c (2026-09-15); this includes reviewed
beta 94f938d3 and unmerged VP-0188. GitHub default/current beta remains
v1.3.005-beta at 94f938d3. The user-selected local tip overrides the normal
remote-beta starting-base rule and the tracker's old confirmation wording.
Source branch: codex/vp-0189-crop-stability.
Source worktree: E:\codex\videoprocessor\vp-0189-crop-stability.

Readiness: no configuration/crop threshold retuning or new renderer ABI is
needed. The render-thread final crop policy owns presentation after subtitle
inspection and before NLS/aspect-limit mapping; source generation, sequence,
epoch and current evidence are available there. Add bounded recovery there so
logical geometry publication and stable-bar timing remain unchanged. Use the
existing quarter-second, distinct-source-sample recovery scale for ambiguous
re-entry; ordinary acquisition outside an unresolved event is unchanged. Within an
unresolved event, a trusted owner alone cannot bypass current safety/proof;
the received log demonstrates why. Keep
outward visibility immediate. Near-black recovery retains current proof and
saved-contract checks while accepting contained observations and revalidating
after outward content clears. Diagnostic state is renderer-owned and must not
perform additional scans; opt-in detail will have a bounded sample budget.

Existing baseline probes and unit-test infrastructure provide deterministic
regressions; add pixel fixtures and composed wiring checks, then build/test x64
Release and prepare a tester package. No original log is required for these
steps. Physical star-field/incident acceptance remains a Review requirement.
No deployment or tester message is authorized by this start.

## Received log and revised readiness (2026-09-16)

The original `vp20260915-24.49.log.txt` was received before source edits.
[Independent log review](../assets/VP-0189/received-log-review.md) confirms 79
changes at 00:30:08–00:30:39 and seven late changes including initial acquisition
at 00:39:17–00:39:18. The late burst alternates trusted/refinement owners against
withdrawal with outside-band content, so the final recovery gate must cover
those owners too. Three returns in the main burst also use trusted ownership.

The 54-second release is verified, but its first episode ends one source frame
after entry; full-raster authority and later reacquisition follow. It is not
valid to attribute the whole interval to the sticky episode or guarantee that
the planned repair resolves it. Preserve full-raster authority and add tests
that distinguish it from an unresolved presentation withdrawal.

The tester identifies the main event as dark action near the end of Kill
Boksoon and reports credits flicker. Before the late burst, logged final layouts
and crop are constant; retain the credits complaint as unresolved and obtain
time/symptom correlation. The late burst is visibly changing geometry in the
log, but the video/content association is not known. Add both intervals and
the long release to the tester acceptance matrix.

No source changes had been made when this evidence arrived. Resume implementation
with the revised ownership contract and the reproducible evidence preserved.

## Implemented candidate and review handoff (2026-09-16)

Implementation is committed and published as
[`911e2b7f`](https://github.com/billslack2/videoprocessor/commit/911e2b7fe46b95560008ffc615d89a5baf6fe2e1),
branch `codex/vp-0189-crop-stability`, based exactly on the user-selected local
`3acd02b3`. No integration merge or deployment was performed.

- Final crop recovery now holds temporary full raster across pixel-safe, trusted,
  refinement and pending-inspection owner changes until adjacent current safe
  samples qualify. The recovery interval is 250 ms: seven samples at 23.976/24,
  sixteen at 59.94/60. It also covers horizontal-conflict and explicit fail-open
  withdrawals without overriding those owners. Normal acquisition outside an
  unresolved event remains unchanged; full-raster authority remains immediate.
- Completed current dense subtitle/Fit or episode proof can resolve the event.
  NLS uses full raster while recovery is active; fixed aspect remains explicit.
- Near-black entry recovery accepts contained observations, preserves the exact
  saved crop and revalidates after previously sticky outward content. It checks
  source generation/sequence and rejects repeat/out-of-order proof. An epoch
  change invalidates the saved certificate and returns to current bootstrap.
- Diagnostics separate current measured bands, proposal containment, owner and
  proof rejection; source-crop transitions and two-second unresolved summaries
  include counts and context. Final-layout records correlate actual mapping.
  `VP_CROP_TRACE_FRAMES=600` opts into at most 600 fresh unresolved per-edge
  records per renderer instance using existing samples; no additional logging
  scans or persistent configuration/ABI changes. Unknown detail is explicit.
- Added the standard-library Python helper, exact-field parsing tests and
  [operator/tester instructions](../assets/VP-0189/crop-diagnostics.md).

Validation on the exact committed candidate:

- Full solution x64 Release build succeeded (final incremental build: zero
  warnings/errors). Command: MSBuild.exe VideoProcessor.sln /m
  /p:Configuration=Release /p:Platform=x64.
- VSTest x64 native core: **1,172/1,172 passed**, including the log-derived late
  owner sequence, alternating safety, contained/sticky recovery, repeats/gaps,
  profile boundaries, existing subtitle/NLS/full-raster tests and moving-star
  pixel fixtures inside/outside a known crop and sparse full-height startup.
- Python helper: **5/5 passed**. It independently reproduces 79 source changes
  and 79 final-layout changes in the original 00:30 interval.
- One intermediate full run failed the unchanged configuration-cache
  `SameSizeEditWithRestoredWriteTimeInvalidatesCache` test. All five cache tests
  then passed in isolation and the final full run above passed. No cache code
  was changed by VP-0189.
- Release manifest verified 59 immutable runtime files. Final zip integrity and
  packaged host/plugin hashes match the build; active configuration is absent
  (only the example is included). Build identity/hashes are in
  [candidate BUILD-INFO](../assets/VP-0189/candidate-build-info.json).

Tester bundle (30,846,853 bytes):
`C:\Users\bslac\Documents\ChatGPT\Done\VP-0189-911e2b7f-x64-Release-tester.zip`
SHA-256: `baeb0406d32b60f7434ffec8fa6d3908a016c5109313b5d51df8982a98f54c6f`.
Local build/test evidence:
`E:\codex\videoprocessor\vp-0189-crop-stability\artifacts\vp0189`
(`build.log`, `core-911e2b7f.trx`, `package.log`).

Review remains open for physical playback, visual acceptance and live enabled/
disabled logging overhead/volume. Source inspection confirms logging consumes
existing evidence and does not feed policy decisions; this is not a measured
hardware overhead claim. Fixed-grid sampling can still miss isolated stars.
The credits complaint and the 54-second interval remain unresolved field
observations, not claimed solved by these policy tests. Tester should compare
the identified Kill Boksoon dark scene and credits, a repeatable multi-AR
star-field passage, stable bars/full height and subtitles, with times/settings
and raw logs. No message has been sent to a tester by this task.

## Authorized local deployment (2026-09-16)

At the user's explicit request, deployed commit `911e2b7f` x64 Release to
`C:\Videoprocessor\vp`. Replaced and SHA-256 verified both `VideoProcessor.exe`
and `vprenderer\VideoProcessorVPRenderer.dll` against the tested candidate.
The prior pair was backed up and verified under
`C:\Videoprocessor\vp\backups\VP-0189-before-911e2b7f-20260916-140445`.
No configuration edits were made; six configuration/state/manifest files were
hash-verified unchanged. The application was not running and was left stopped.
Normal crop diagnostics are included; bounded edge tracing was not enabled.
Deployment evidence: [local deployment record](../assets/VP-0189/local-deployment.json).
Story remains Review pending physical playback and visual/performance acceptance.

## Returned playback failure and follow-up (2026-09-17)

The user supplied `vp - Kopie.log.txt` from the 911e2b7f tester build and
identified Apple TV **Women in Blue, S02E06, 10:55–11:30**: a reproducible slow
format change jumps in VP; the tester reports correct following in madVR.
This is a failed field acceptance case, not a completed visual validation.
Exact wall-clock correspondence among several repeated log sequences remains
unconfirmed. Preserve the title/episode as supplied by the user.

Source review finds repeated nested confirmation restarts while authoritative
windowbox bounds move, coarse stable-geometry deadband steps, horizontal
outside-band conflicts causing full-raster withdrawal, and two recovery events
lasting 111,782 / 108,437 ms. During the long events the old bottom=1948
contract repeatedly disagrees with current trusted bottom=1952; excluded bands
can be safe while strict containment and an inspection latch block recovery.
Do not attribute all of either interval solely to the new recovery gate.

User authorized correction and thorough review, with physical playback planned
for 2026-09-18. Continue the explicitly selected local-tip lineage on
`codex/vp-0189-crop-stability` (911e2b7f parent 3acd02b3); remote latest beta
was re-discovered and fetched at v1.3.005-beta / 94f938d3, unchanged.
Add regressions before policy edits. Prefer conservative outward reconciliation
of newly affirmed edges over relaxing containment or black/star thresholds.
Preserve the existing four-second guard against brief nested dark content,
but stop restarting its proof for continuous authoritative geometry motion.
Review recovery/inspection composition, reset behavior, and logging accuracy.
No new deployment or physical acceptance is implied by this work.

## Follow-up reviewed candidate (2026-09-17)

Published [884029b1](https://github.com/billslack2/videoprocessor/commit/884029b15e87fb2ad3077299f1843f61b5c88a21)
on `codex/vp-0189-crop-stability`, directly following 911e2b7f on the
user-selected 3acd02b3 lineage. No integration merge or new deployment.

[Detailed review and comparison plan](../assets/VP-0189/women-in-blue-review.md)
records source conflicts, upstream primary-source comparisons, and uncertainty.
[Extracted evidence](../assets/VP-0189/women-in-blue-evidence.json) preserves
original line numbers; [extraction script](../assets/VP-0189/review_women_log.py)
reproduces the timing and diagnostic selections from the raw supplied file.

Four repeated sequences last 32.157–32.449 seconds, consistent with the supplied
S02E06 10:55–11:30 passage. The 05:29 and 05:34 passes reproduce identical
geometry changes at identical source-frame offsets, with 6,422 / 4,547 ms
full-raster holds. Separate 111,782 / 108,437 ms recovery events are not the
duration of the animation. Exact playback-time/log-clock alignment remains for
the planned 2026-09-18 viewing.

Implemented corrections:

- Safe sampling-scale differences reaffirm the unchanged logical crop across
  inspection and both recovery paths. Four pixels do not publish a new aspect.
  Require the same bar axes and current safety for the exact saved rectangle;
  preserve source/epoch/full-raster gates and quarter-second inward proof.
- A horizontal conflict can use an outward Fit only with complete current
  bounded pixel extents, matching source/base, and an envelope that includes
  those extents and the detector geometry. No stale/partial/translation proof
  or existing recovery can use this to bypass its visibility/ownership rules.
- Nearby continuous affirmative nested observations keep their first proof
  sequence while coordinates move; gaps, missing evidence and scene resets
  cannot bridge the guard. Starting a novel candidate clears stale remembered-
  geometry provenance.
- Added owner reasons, inspection latch, safe sampling equivalence, accepted
  horizontal proof and nested proof age to diagnostics. Fixed closed-event
  duration aggregation and expose actual fill rectangles in the helper.

Our existing 2% geometry deadband and four-second newly nested-axis guard are
unchanged. Four seconds is our design choice, not a standard or an externally
imposed constraint. A finer tracking experiment was removed because it could
chase alternating detector noise. Preserve normal stable behavior and assess
remaining animated scale steps and guard timing during playback.

Validation:

- The moving-nested-confirmation regression failed on the original 911e2b7f
  policy. The corrected native suite includes steady four-pixel/no-new-aspect,
  composed inspection/recovery, raw-versus-constrained near-black evidence,
  unsafe/stale/provisional rejection, bounded horizontal Fit, missing evidence,
  static edge noise, and candidate provenance cases.
- A clean x64 Release rebuild passed all 1,181 then-present native tests. The
  final exact-commit x64 Release build passed **1,182/1,182** native tests.
  Python helper **7/7** passed. Existing pixel/star, subtitle, NLS, generation,
  full-height and crop-policy regressions remain included.
- An intermediate incremental link failed with MSVC LNK1103 corrupt debugging
  information in ConfigFileTests.obj; the clean rebuild resolved it. The clean
  build reports existing config-header shadowing warnings, not new crop errors.
- Release manifest verified 59 immutable files; packaged host/renderer hashes
  match the exact build and zip CRC verification passed. No active configuration
  was changed. [Build identity](../assets/VP-0189/women-candidate-build-info.json)
  and [archive details](../assets/VP-0189/women-candidate-package-info.json).

Tester archive:
`C:\Users\bslac\Documents\ChatGPT\Done\VP-0189-884029b1-x64-Release-tester.zip`
(29,836,987 bytes), SHA-256
`a923b3c3d9fd8dbc9e69f0cad8fb1784a95b6b950911d6a60db7b1cf4cc3071e`.
Build/test evidence: `E:\codex\videoprocessor\vp-0189-crop-stability\artifacts\women-in-blue`.

Review remains open for physical playback, remaining gradual scale behavior,
the appropriate nested guard, and live diagnostic overhead. Current deployment
remains 911e2b7f. No tester message has been sent. The candidate does not claim
perfectly smooth playback, resolution of every long hold, or star recognition.

## Authorized follow-up local deployment (2026-09-17)

At the user's explicit request, deployed the tested `884029b1` x64 Release
host/renderer pair to `C:\Videoprocessor\vp` at 08:09:50 EDT. SHA-256 verified
both installed binaries against the tested candidate. The previous 911e2b7f
pair was backed up and verified under
`C:\Videoprocessor\vp\backups\VP-0189-before-884029b1-20260917-080950`.

No configuration edits: six configuration/state/manifest files were hash-verified
unchanged. VP was not running and was left stopped, ready for the next launch.
Normal diagnostics are included; bounded per-frame tracing was not enabled.
This deployment supersedes the earlier paragraph saying 911e2b7f remains installed.
Story remains Review pending physical playback. Evidence:
[follow-up deployment record](../assets/VP-0189/women-local-deployment.json).

## Live playback correction and deployment (2026-09-17)

Playback of 884029b1 still showed crop drops. Event 18 at 08:24:00 demonstrated that a pixel-safe four-pixel observation could reaffirm the crop while a separately published coarse envelope forced full raster for 578 ms. The earlier playback fix was incomplete.

Commit a7dc020a91c9c5e32502e58db4ac47af5e7fe6fb suppresses only sampling-equivalent coarse expansions backed by current valid pixel-safe evidence for the exact trusted crop. Real outward pixels and material format changes keep their existing handling. No thresholds or confirmation times changed. A rate-limited pixel-safe-sampling-reaffirmed diagnostic identifies when the new path is used.

Final committed x64 Release build succeeded; 1184/1184 native tests passed. Both binaries were deployed and hashes verified at 08:33:33 EDT. VP was closed gracefully and reopened. Six configuration/state/manifest files were preserved with no deployment edits. Prior pair backup: C:\Videoprocessor\vp\backups\VP-0189-before-a7dc020a-20260917-083332.

[Review and reproduction](../assets/VP-0189/live-sampling-review.md) · [Deployment record](../assets/VP-0189/live-sampling-deployment.json). Logs and screenshot preserved locally under C:\Users\bslac\Documents\ChatGPT\Done\crop-review-20260917-live. Status remains Review pending another physical playback; video is useful if symptoms remain but was not needed to establish this conflict.

## Recorded subtitle failure: reviewed corrective plan (2026-09-17)

The a7dc020a playback recording confirms that the same shot shrinks at subtitle onset after the internal divider is gone. Event 11 at 09:36:20 lasts 11437 ms. A compiled probe of the deployed source reproduces three interacting gaps: mixed edge movement bypasses the outward guard, subtitle deferral is inactive during initial confirmation, and two provisional samples can restore remembered geometry despite intended deferral.

[Reviewed implementation plan](../assets/VP-0189/subtitle-crop-fix-plan.md) covers an integrated failing regression, explicit transition admission across history/look-ahead paths, per-axis outward checks and subtitle onset arbitration, and current-presentation safety proof for bounded recovery. It includes adverse-case review, regression coverage and physical playback acceptance. [Probe results](../assets/VP-0189/subtitle-crop-plan-probe-results.txt) and [probe source](../assets/VP-0189/subtitle-crop-plan-probe.cpp) support the findings. Review was against code, existing tests, video and executable probe, not an external independent review.

No source/runtime changes or deployment were made for this planning request. Status remains Review; playback is not accepted. The proposed visual overlay remains optional and separate from the corrective scope.

## Subtitle regression and correction completed (2026-09-17)

Step one added a sequence regression through shared production admission, subtitle-routing, crop and recovery policies. It reproduced the subtitle-triggered false aspect change. The behavior-preserving extraction passed existing tests; the new desired-behavior regression initially failed as expected.

Following authorization to continue, the correction in source commit e3d4e856 made current transition deferral binding on remembered geometry and queued publication, and checked expansion per edge. The reproduced sequence now passes across the tested frame-rate families. Additional tests cover real expansion, invalid evidence, confirmation continuity and an internal split-screen divider. Full Release validation and diagnostic-helper checks passed.

Local deployment is complete. Runtime identity and binary verification succeeded, and user configuration was preserved. Detailed machine-specific evidence remains local. Recovery safety requirements and existing thresholds/timers remain unchanged because the reproduced failure is prevented before recovery is armed.

Status remains Review pending physical playback of the gradual transition and subtitle scene. The optional graphical diagnostic overlay is not implemented.


## Final tested integration; Review retained (2026-09-17 EDT)

The user authorized pushing and packaging the successfully tested build, then
explicitly requested merging while retaining Review. Source PR
[#94](https://github.com/billslack2/videoprocessor/pull/94) is merged into
`v1.3.005-beta` at `cb1808578f7d34b675a82a9802c8c0b6e39a315f`.
Its tree `26e83b42eb4560dec5cc1c3055a586986826cc0b` is identical to tested head
`525e58022dc51f6d75591a3c4838447cdd683991`. This includes the deliberately
selected VP-0188 local baseline `3acd02b3`; prerequisite PR #93 is also merged.

Final policy supersedes the earlier experimental four-second nested guard:

- Established crops retain all-sided inner compositions and use a **5%**
  inward aspect tolerance anchored to the accepted rectangle. The obsolete
  nested timer and pause/resume machinery were removed.
- Live, history and queued decisions obey the same current-evidence and
  accepted-reference rules. Small sampling changes and temporary subtitle
  presentation adjustments do not independently redefine program geometry.
- A real-pixel regression reproduced an invented format when one bar axis
  failed validation. Explicit axis metadata and a narrow inward-publication
  gate now retain the complete established crop in that case. Startup,
  measured outward visibility and genuine multi-AR changes retain their paths.
- Bounded diagnostics distinguish proposed/trusted/accepted/post-fill bounds,
  failed axes and incomplete scans. Full-extent support is diagnostic-only;
  no general full-extent classifier or artistic-intent recognition is claimed.

Validation: exact-commit clean x64 Release build, **1,229/1,229 native tests**,
**7/7 Python crop-log analyzer tests**, and independent architecture/mpv-script
reviews without remaining blockers. New coverage includes pixel failures in
both orientations, history/current-frame queued adoption, intermediate inward
proof, startup, outward visibility and repeated 2.20/2.35, 2.35/1.90 and 2.20/1.43
changes. User playback acceptance: "everything I watched look good."

The deployed host/renderer remain the verified `525e5802` pair; both report
`dirty=0`. Six configuration/state files were preserved with no edits.
Backup: `C:\Videoprocessor\vp\backups\VP-0189-before-525e5802-20260917-194007`.
The merge does not require replacing these byte-identical-source tested binaries.

Share ZIP: `VideoProcessor-v1.3.005-beta-VP0189-525e5802-x64.zip`, 30,850,623 bytes.
All 59 release-manifest files were hash-verified, including the runtime pair.
SHA-256: `c501194bb2a140a515569f0fe07104e0b65e9e5d4ee3c87141d97b397c39f666`.
Local sharing folder: `C:\Users\bslac\Documents\ChatGPT\Done\VP-0189-525e5802`.

Review remains open by user request for continued field coverage. No claim is
made that every earlier dark/credits/star-field report has been individually
reproduced and accepted. Stronger full-extent calibration and the optional
graphical overlay remain outside this completed correction. The separate
bottom-positioning/zoom-to-fill report was not added to this development.

## September 19 follow-up merge; Review retained

[PR #95](https://github.com/billslack2/videoprocessor/pull/95) merged ten reviewed
follow-up commits into `v1.3.005-beta` at `8414b11633d3ab8cc06fa3964c273c81b76f05bc`.
Merge tree `1a3af473d75fa5cb4eb528f034169fd5c9039297` exactly matches clean tested
head `6dc92012006ba33a1c43a68049c2920b91dbe538`.

- Retain established crop during unavailable acquisition only while excluded
  bands remain pixel-safe. The retention comment documents the intentional
  near-black exception.
- Distinguish entering darkness from independent cut evidence. A cut revokes
  full-frame authority even during scene-notification cooldown; two fresh,
  non-dark exact-full measurements can reaffirm existing full-frame authority.
  Stale/repeated frames, changed contexts and contradictory evidence cannot.
- Add opt-in source black-level/chroma and color-picture diagnostics. Per-renderer
  instance IDs disambiguate restarted sample counters. Color evidence stays
  shadow-only: noisy/tinted bars remain an unresolved classification ambiguity.
- Prefer native progressive 25/30 Hz before doubled fallback; retain the
  interlaced refresh-selection path.
- Remove the action-triggered window-placement guard and preserve layout-before-
  renderer-notification ordering. Window size/position remain user-owned.

Validation: clean x64 Release build, **1,288/1,288 native tests passed** at the
exact deployed commit. Regression tests failed before fixes and passed afterward
for cooldown cut revocation, fresh full-frame reaffirmation and native window
placement. A fresh independent final review found no new actionable blocker.
The new tests do not substitute for every real MFC, shell or external-action path.

The paired host/renderer were backed up, deployed and hash-verified. Both logged
clean `6dc92012` identities and active frame processing. Configuration files were
preserved. Binary backup: `C:\Videoprocessor\vp\backups\before-reviewed-6dc92012-20260919-094120`.
Build/TRX, review and deployment artifacts are locally retained under
`C:\Users\bslac\Documents\ChatGPT\Done\imax-starfield-investigation-20260918`.
No additional deployment is needed solely for the tree-identical merge.

Subsequent user testing exposed a separate external-script unmaximize action:
`C:\Videoprocessor\utils\madvr_sendkeypress.ps1` used NirCmd `win activate`, which
a hidden native-window reproduction proved restores maximized windows. Its local
replacement restores only minimized windows, requests foreground focus, and keeps
the existing foreground check before shortcut delivery. Four installed-helper
window-state tests passed. The original script is backed up at
`C:\Videoprocessor\vp\backups\hdr-activation-20260919-095335`.
This installation-specific script is outside the repository and outside PR #95;
real shortcut delivery after the change still needs playback validation.

**Keep VP-0189 in Review.** Continue field testing dark logos/credits/star fields,
multi-AR transitions and full application/window interactions. No general claim
of color-based aspect detection or universal scene correctness is made.
[VP-0190](../backlog/VP-0190_preserve-active-picture-authority-through-NLS-only-profile-changes.md)
remains a separate Backlog story for NLS-only profile changes invalidating picture
authority. It is not fixed or closed by this merge.

## September 19 trailer follow-up; Review retained

[PR #97](https://github.com/billslack2/videoprocessor/pull/97) merged the retained
near-black episode lifecycle correction into v1.3.005-beta at
ad50ebd866fc66b2743351a0f493a470a3ff4ab1. The merge tree matches clean tested/deployed
04e09cb4bce41edb20ccbf3a9b6fba179250c14f, including the previously merged VP-0191 profile fix.

RETAIN_CROP now ends when the existing exact-entry, current-context,
consecutive-frame pixel-safety proof establishes that bright scope has returned.
Normal bar/subtitle handling resumes without changing the displayed rectangle.
No threshold relaxation, new timer, or new aspect authority was introduced.

The lifecycle regression failed before the production fix and passed afterward.
Additional tests cover rejected stale/invalid proof, provisional-observation and
recovery handoff without a framing flash, duplicates, missing frames, and
interrupted proof. Two independent reviews found no blocking defect.
The clean integrated x64 Release solution passed 1293/1293 tests.
An incremental link initially reported corrupt debug information; a clean rebuild
resolved it before deployment.
Two integrated full runs failed the existing
ProfileChangeDisplayDurationIsBoundedAndLive test's error-message assertion.
It passed alone, with its complete test class, and in a diagnostic full run.
A one-line diagnostic improvement now prints the actual error on failure;
assertions and production profile/schema code are unchanged. The cause was not
established. The initial failures remain recorded in the retained TRX evidence.

The paired host and renderer were backed up and their deployed SHA-256 hashes
verified against the same clean build. No configuration edits were made.
Backup: C:\Videoprocessor\vp\backups\before-reviewed-04e09cb4-20260919-112128
Deployment and test evidence:
C:\Users\bslac\Documents\ChatGPT\Done\scope-trailer-20260919-1048

The trailer log proves temporary full-raster presentation while accepted scope
geometry stayed unchanged, but does not identify the unrecorded pixels. This
correction does not claim to prevent every observed release; genuine outward
content during a still-dark episode retains its visibility safeguards.

**Keep VP-0189 in Review.** Proceed with regular viewing validation, including
dark-to-bright transitions, subtitles/menus, and real aspect changes. Preserve
the log and approximate clock time if a shrink recurs. VP-0190 remains separate.

## September 19 sampling-step regression and shared retention follow-up

Status remains **Review**. Source worktree:
E:\codex\videoprocessor\pixel-safe-crop-reaffirmation,
branch codex/pixel-safe-crop-reaffirmation, based on the verified current beta
73c850041d284e3e37c959a8956d896b79106aa1.

Andrew's vp73c85004.log.txt contains 21 matching releases: retained scope
0,276-3840,1884 versus provisional 0,276-3840,1888, with safe broad bar samples.
Recoveries last 281-297 ms. The local session reproduces the same signature.
This build already includes PR #97; PR #98 only changed packaging. The logs
show no raster or anamorphic-scale change. Four rows are one acquisition step
at 4K, not proof of a pixel-aspect-ratio change.

Initial candidate 318cec4a deployed cleanly but did not resolve replay:
at 19:45:17 and 19:45:29 the focused strip check reported a conflict despite
safe broad bar statistics. The initial strict-pixel correction was therefore
insufficient.

Current candidate d6ad6dc18e020086140ed15a4943c52a7ede92d6 separates black-pixel
evidence from permitted presentation tolerance. Established scope may retain
its exact rectangle across a one-step provisional vertical discrepancy when
width and broad excluded bands remain valid, even if that narrow strip has
bright or colored pixels. This intentionally allows up to four source rows
of edge tolerance at 4K. It does not advance the reference rectangle, grant
new aspect authority, or accept larger/other-axis conflicts. The same
interpretation resolves vertical inspection and qualifies current samples
toward ordinary recovery's existing dwell. Pixel conflicts remain visible
in diagnostics, with peak luma/chroma measurements.

Red tests reproduced initial crop withdrawal, inspection latching, active
recovery rejecting current retention, and bright-border withdrawal. The final
clean x64 Release build succeeded. Final full run: **1302/1302 passed**.
The preceding full run had one ConfigurationReadCacheTests failure,
SameSizeEditWithRestoredWriteTimeInvalidatesCache (expected two, got one).
Its full class and the complete unchanged-binary rerun passed. No cache code
or assertions were changed; cause remains unestablished and initial TRX is
preserved. Incremental linker debug-info corruption required clean rebuilds.

Paired host/renderer deployed with matching SHA-256 hashes. No configuration
edits. Backup:
C:\Videoprocessor\vp\backups\before-shared-edge-d6ad6dc1-20260919-195855

Build, red/green test and deployment evidence:
C:\Users\bslac\Documents\ChatGPT\Done\scope-sampling-d6ad6dc1-20260919

This source candidate is locally committed, not yet pushed or merged.
Replay/regular-viewing validation remains required; retain VP-0189 in Review.


## September 19 merge and installer release follow-up

**Keep Review.** The previously local candidate is now pushed and merged:
https://github.com/billslack2/videoprocessor/pull/100
Beta merge 642a1146f2d7d963a47d7f4f4b9791ecf94aa8f2.

Live d6ad6dc1 replay from 20:05:49 through 20:09:19 logged 62 small-edge
tolerance decisions with zero crop releases or applied rectangle changes.
The user believed the earlier larger intrusions were playback controls.

Reusable merge/release skill and scripts are checked in and merged:
https://github.com/billslack2/videoprocessor/pull/101
Merge 28dd9749db7672782d218d64b626fd9cbd33bc80.
Personal skill: C:\Users\bslac\.codex\skills\vp-release.
It pins source/base identity, refuses changed inputs and existing output folders,
and gates export on validation. Explicit receipt-verified resume repeats tests;
failures are preserved, not automatically retried. Byte-identical PE/installer
rebuilds are not claimed.

Installer/ZIP source ce11fa05bc119734a8d6a7a3ac18bf47a27e79a1 is pushed on
codex/crop-release-20260919. Application sources match crop-fix beta 642a1146;
installer tooling is pinned to draft PR #99 commit
4ed2980a8368a96ae3ef931e6a2f5fbecb0895ff. PR #99 remains unmerged.
Clean x64 Release build completed. Final gated run passed 1302/1302 plus
49 preservation/recovery, 11 identity and 23 runtime checks. All 70 ZIP payload
files were independently verified against the manifest.

The initial full run again hit the unchanged configuration-cache test; isolated
class runs on current/baseline and two subsequent full current runs passed.
No cache fix is claimed. Initial failure and subsequent results are preserved.
Real installer QA safely refused the active user Config tray process; production
registry values remained unchanged and no QA registration was created. Lifecycle
and clean-machine interactive coverage for this exact payload remain incomplete.
No deployment or public GitHub Release occurred. Setup is unsigned.

Artifacts, SHA-256 sidecars, receipts and qualification notes:
C:\Users\bslac\Documents\ChatGPT\Done\VP-scope-fix-20260919

Filename follow-up: setup is now named
VideoProcessorSetup-1.3.005-beta-ce11fa05bc11.exe.
The SHA-256 is unchanged; checksum sidecar and release receipt now reference
the corrected name. Clean installer names always include the 12-character
source commit; dirty names additionally retain the source fingerprint.
The generator correction is pushed to draft installer PR #99 (8fd165da),
and the corresponding workflow correction is merged through PR #102.
The new naming assertion failed before the fix; all 12 identity checks passed
after it. No application rebuild or deployment was needed for this rename.

## September 19 Eternals scrolling-text crop admission correction

**Keep Review.** A further incident on deployed d6ad6dc1 at 21:24:59-21:25:00
changed full raster to 0,476-3840,1688 (3.168:1) for ten 24-fps source frames.
The owner said bar-refinement retention, but that crop had not been presented;
current outside bands were unsafe and subtitle confirmation was also pending.
The user did not reproduce it after rewinding. This identifies the logged
presentation excursion, not a proven subtitle classification of the text.

Local candidate 654ccade078ec44e3945079bd5b273e98d9c1a3c on
codex/crop-retention-admission, based on freshly fetched beta 525767f6:
E:\codex\videoprocessor\crop-retention-admission.

A final admission check now requires current picture acquisition authority or
an identical previously admitted crop. Retention/subtitle handlers may preserve
an established picture, but cannot introduce unpresented geometry. It preserves
history through temporary withdrawals and clears it across authoritative full
raster, source/raster/epoch changes or automatic-crop disable. Fill/NLS cannot
reintroduce a blocked candidate. No timer or pixel threshold was changed.

Four regression tests failed with unchanged behavior; the aspect-change control
passed. After correction, clean x64 Release build and all 1307 tests passed.
Coverage includes twelve presentation owners, real aspect changes in both
directions, four-pixel sampling and provisional recovery. Earlier failed fixture
assertions and the LNK1103 incremental build failure are preserved alongside the
successful clean rebuild; no test failure was discarded or silently retried.

Evidence: C:\Users\bslac\Documents\ChatGPT\Done\crop-admission-654ccade-20260919.
Details: docs/VP-0189_CROP_PRESENTATION_ADMISSION.md in the candidate checkout.
The candidate is locally committed, not pushed/merged/deployed. Build validation
preceded the local commit; a deployment should rebuild its clean identity.
The running installation was left unchanged. Replay validation remains pending.

## September 19 crop-admission deployment and distributable build

**Keep Review.** User authorized deployment and a ZIP/installer for the tested
candidate. Release branch codex/crop-admission-release-20260919 is pushed at
29ea0339b8029879258b8fde4334928c6d8e01b5. It combines application candidate
654ccade078ec44e3945079bd5b273e98d9c1a3c with installer tooling pinned to
8fd165da519851cc265e8bea7bac35c9554c956b (draft PR #99). Application sources
match 654ccade exactly. Neither was merged into beta by this task; remote beta
at build remained 525767f6. The release wrapper's BetaCommit baseline parameter
pinned the requested application candidate, explicitly recorded in provenance.

Clean x64 Release rebuild and 1307/1307 tests passed, with 49 installer support,
12 identity and 23 runtime-packaging checks. All 70 ZIP payload hashes and exact
archive membership were independently verified. Artifacts and receipts:
C:\Users\bslac\Documents\ChatGPT\Done\VP-crop-admission-29ea0339-20260919.

- VideoProcessorSetup-1.3.005-beta-29ea0339b802.exe
  SHA256 665D2F3228E978780476FCFC06C349F20D4E191B0AF5F818026B84CC2EA78402
- VideoProcessor-1.3.005-beta-29ea0339b802-x64-Portable.zip
  SHA256 4FED16C9BB0F7EACF8040A94B314791B05F016FA1592AF76725E7F3AE0EAED99

At 21:54:10, deployed the paired host/renderer to C:\Videoprocessor\vp and
launched VP (PID 24136). Both clean build identities appear in the live startup
log, and deployed hashes match the payload. No configuration edits occurred.
Backup: C:\Videoprocessor\vp\backups\crop-admission-29ea0339-20260919-215409.
Deployment details, running-build log, scripts and hashes accompany the release.

Setup is unsigned. Real installer lifecycle, clean Windows without Visual Studio,
and interactive Config qualification remain untested for this exact payload.
Production installer registration at C:\vptest was preserved; this deployment
used the requested active runtime path directly. No GitHub Release was published.
Replay testing of the scrolling-text transition remains the next validation.

## September 19 picture transition and small-measurement correction

**Keep Review.** The user reported stutters on deployed 29ea0339 and requested
QA/colleague review, with special attention to recurring small measurement changes.
The preserved 22:08:15 log shows established scope 0,276-3840,1884 briefly taking
dense subtitle FIT geometry 0,20-3840,2140 before the picture model publishes
0,68-3840,2092. A queued old-reference mismatch (280 versus 276) delayed the
publication; FIT briefly won presentation during confirmation.

Local candidate 40ad343edb7423705107fb369c6d5a14cd9120ff:
codex/crop-transition-handoff, E:\codex\videoprocessor\crop-transition-handoff.
Started from freshly discovered/fetched beta 525767f6 and carried application
correction 654ccade, matching the prior deployed application's sources.

Fresh broad picture evidence now reserves the old presentation for the existing
bounded picture-confirmation sequence. One detector step (4 pixels at 4K,
2 at 1080p) is tolerated at the relevant proof, old-reference, and remembered
pending-geometry boundaries. Proof stays anchored; tolerance cannot walk or
renew the hold indefinitely. Publication uses the current trusted coordinates
after normal authority checks, avoiding a stale four-pixel candidate that fails
same-frame pixel reinspection. Established geometry remains stable afterward.

Exact current queued-target validation, current strip certificates, admission,
recovery, near-black guards and real format-change evidence remain required.
Existing FIT/translation at transition start retains its path. No enlarged AR
deadband, brightness threshold, additional pixel read or four-second timer.

RED evidence includes the original handoff, preexisting-FIT and indefinite-jitter
review findings, actual pixel-backed current-versus-candidate commitment failure,
and a further remembered-history failure exposed by repeating the round trip.
After fixes, clean x64 Release solution rebuild and **1336/1336 tests passed**
(29 added). Both independent reviewers found no remaining blocking issue.
Tests cover one/both-edge jitter in both orders, lookahead/live paths, stale
queued targets, multiple dense-analysis cadences, repeated round trips, excessive
drift and authority/context vetoes. Outward replay remeasures actual P010 source
bytes; inward legs use policy evidence, not decoded content replay.

Evidence and commit/build/test receipt:
C:\Users\bslac\Documents\ChatGPT\Done\crop-transition-handoff-20260919.
Design: docs/VP-0189_PICTURE_TRANSITION_HANDOFF.md in the source checkout.
Candidate is locally committed, not pushed, merged, deployed or packaged.
Validation preceded the commit and source snapshots match; deployment must
rebuild its clean committed identity. Live replay remains pending. Running
installation and configuration were left unchanged.

## September 19 deployment of transition correction 40ad343e

**Keep Review.** User requested deployment. Rebuilt clean committed source
40ad343edb7423705107fb369c6d5a14cd9120ff in x64 Release; all 1336 tests passed.
At 23:09:41, deployed the matching host/renderer pair to C:\Videoprocessor\vp
and launched VP (PID 36548). Startup identities for both modules report this
commit with dirty=0; both deployed hashes match the qualified build. The new
picture-handoff diagnostics are active. No configuration edits occurred;
VideoProcessor.cfg hash remained unchanged before and after launch.

Rollback pair:
C:\Videoprocessor\vp\backups\transition-handoff-40ad343e-20260919-230940.
Build/test logs, deployment receipt, startup log and SHA-256 records:
C:\Users\bslac\Documents\ChatGPT\Done\crop-transition-handoff-20260919.
No new ZIP, installer, source push or beta merge. Replay validation is now ready.

## September 19 buffered expansion confirmation

**Keep Review.** The user requested that the existing lookahead supply the
three-frame picture confirmation, followed by independent review. The preserved
40ad343e log contains seven scope-to-taller transitions: six waited three source
intervals (125 ms at 24 fps), one waited two (83 ms). Existing preview evidence
was collected after dequeue and rejected by the separate live proof gate.

Local candidate e0d226ad1e6e0a4ee3d27fe19d23acbb2a4a95f5:
codex/crop-lookahead-proof, E:\codex\videoprocessor\crop-lookahead-proof.
Started from freshly fetched beta 525767f6, carrying the tested 40ad343e changes.

Preview now inspects current plus two already buffered source frames before
consuming current. A narrow full-width, both-edge vertical expansion can publish
on its first changed frame when all three samples provide broad, non-near-black
picture evidence. There is no extra queue wait or depth increase. The proof uses
first-frame coordinates and does not advance live state to future counters.

Exact current pixels, model admission, subtitle ownership, anchored four-pixel
noise tolerance, final admission and recovery remain active. Continuity, capture
counter gaps, source/viewport/policy context and still-queued identities are
checked. Insufficient or ambiguous evidence retains the normal fallback.
Additional inspection timing and current-proof validation are logged.

RED: the original live gate rejected first-frame publication despite three real
P010 pixel-backed observations. GREEN: clean x64 Release solution rebuild and
**1350/1350 tests passed**. Fourteen added tests cover proof, continuity, stale
state, subtitle conflicts, repeated transitions and subsequent clean-bar jitter.
All twelve changed/new source files match the pre-build snapshot. Two independent
final reviews found no concrete blocker. Review identified and closed a timeline
continuity-stamping loophole. A compile namespace typo in the final preflight was
corrected and the full clean build rerun; failed-build evidence is preserved.

Actual picture growth is tested separately from detector jitter. That pixel
control does not establish a visible bounce or justify a stricter future-pixel
guard. Live HDMI cadence, resize/profile changes, perceptual smoothness and
inspection cost still need playback validation.

Design: docs/VP-0189_BUFFERED_EXPANSION_PROOF.md in the source checkout.
Build/test/source snapshots and receipt:
C:\Users\bslac\Documents\ChatGPT\Done\crop-lookahead-proof-20260919.
Source is committed locally, not pushed, merged, deployed or packaged. Validation
preceded the commit; deployment requires rebuilding the clean committed identity.
Running installation remains 40ad343e, and configuration is unchanged.

## September 19 deployment of buffered proof e0d226ad

**Keep Review.** User requested local deployment. Rebuilt clean committed source
e0d226ad1e6e0a4ee3d27fe19d23acbb2a4a95f5 in x64 Release; all 1350 tests passed.
Deployed the paired host and renderer at 23:46:35 and launched VP, PID 31504.
The live log confirms both host and renderer identities at e0d226ad with dirty=0;
loaded renderer path and both deployed hashes match the qualified build.
Capture and presentation telemetry are active. No configuration edits; the
configuration hash remained unchanged after launch.

Rollback pair:
C:\Videoprocessor\vp\backups\lookahead-proof-e0d226ad-20260919-234635.
Build/test logs, deployment script and receipt, startup log and hashes:
C:\Users\bslac\Documents\ChatGPT\Done\crop-lookahead-proof-20260919.
No source push, merge or packaging. Ready for live transition replay validation.

## September 20: live-validated crop stack merged through PR #103

**Keep Review.** The user replayed the gradual scope-to-16:9 scene several times,
then tested a broad range of content, reported that it looks good, and explicitly
authorized check-in and merge.

[PR #103](https://github.com/billslack2/videoprocessor/pull/103) merged into
`v1.3.005-beta` at `a6914dc6e09ce95b4c9373f17ea9a5a74e0612ec`.
The merged tree is byte-for-byte Git-tree identical to tested/deployed feature
commit `2bab85e38303ca3360fc3017c1b10ed5b740f187`; ancestry and tree equality were
verified after fetching the remote beta. The prior beta was `525767f6` and had
not advanced before merge. GitHub reported the PR mergeable with no pending or
failed checks; no Actions runs/checks were listed for the feature head. No check
was bypassed.

The integrated stack includes crop presentation admission (`654ccade`), stable
picture handoff (`40ad343e`), buffered outward proof (`e0d226ad`), Alien dark-entry
recovery (`75651be5`), and gradual-motion handling (`2bab85e3`).

Validation: clean committed x64 Release build, **1374/1374 native tests passed**,
zero failures/skips, two independent reviewers with no remaining blockers.
Movement was injected into existing hard-change, jitter, subtitle, general
recovery and Alien fixtures as well as new real-P010 ramp tests. These tests
caught and drove corrections for premature old-crop return and repeated motion
reactivation during local settlement. Tested hard-change frame timing remains
unchanged.

Deployment at 08:35 EDT replaced and verified the matching host/renderer pair:
- Host SHA-256: `BDCF6B3A9942F8126F7CC5B3E300B7EFFB2D9AA469818AC55FEE5E667062A551`.
- Renderer SHA-256: `8217D9E2B0E0574E32D23A727574AA5D155AF5BDF00A27CE45D709A23588F188`.
- Backup/receipt: `C:\Videoprocessor\vp\backups\moving-picture-2bab85e3-20260920-083541\deployment.json`.
- Test results: `E:\codex\videoprocessor\moving-picture-transition\TestResults\moving-picture-deployment-full.trx`.

Both live module identities subsequently reported clean `2bab85e3`. In the
11:00:14-11:00:19 replay, the full-source presentation stayed unchanged during
the entire recognized expansion: zero intermediate resize steps, compared with
ten previously. Final full-raster authority caused no extra resize; subsequent
scope reacquisition and a hard full-raster change completed normally. Queue depth
was healthy. Live evidence: `C:\Users\bslac\Documents\ChatGPT\Done\VP-moving-bars-live-20260920\review.md`
and the preserved session/evidence logs in that directory.

One residual observation remains: an earlier return to scope briefly reused a
safe intermediate crop for one source frame (~42 ms) before correcting. Physical
DXGI frame statistics were unavailable, so the logs do not certify every display
presentation. The user nevertheless reports good behavior across broad content.

No configuration changes, additional deployment, installer, or ZIP accompanied
this merge. The currently installed runtime remains the tested `2bab85e3` pair.

## 2026-09-21: sampling retention and bounded presentation follow-up

Merged [PR #109](https://github.com/billslack2/videoprocessor/pull/109) into
`v1.3.005-beta` at `6785d0b1092b445a8b05136c2bce1249a09e7d4f` after the
user reported the asymmetric-side crop loss appeared fixed and approved merge.
The recorded Done status from the explicit 2026-09-20 closure is preserved.

The correction keeps established scope framing through contained horizontal
measurements plus one scan step at each vertical edge, subject to existing
pixel/context checks. Actual outward content can use its measured envelope
instead of full-raster fallback. Exact build labels avoid ancestor RC tags.
The user clarified that Paramount menu fitting on a regular 16:9 desktop can
legitimately show black on all four sides of the configured CIH screen area.
The reported three-frame change is not established as an unwanted bounce:
seeking/menu interaction could explain it. No speculative handoff change was made.

Both sampling regression tests failed before the fix. The tested feature commit
`efadc3d0caeff492799960ea61e0fd6bcc612dbc` passed 1,395 native tests and was
locally deployed with both module identities verified. A fresh detached checkout
of the actual merged beta also passed all 1,395 tests, installer support/identity
and runtime packaging checks. All 70 ZIP payload entries match the manifest.

Clean merged-beta build and artifacts:
- Checkout: `E:\codex\videoprocessor\release-crop-6785d0b1`.
- [Portable ZIP](C:/Users/bslac/Documents/ChatGPT/Done/VP-beta-6785d0b1092b/VideoProcessor-1.3.005-beta-6785d0b1092b-x64-Portable.zip).
- [Setup EXE](C:/Users/bslac/Documents/ChatGPT/Done/VP-beta-6785d0b1092b/VideoProcessorSetup-1.3.005-beta-6785d0b1092b.exe).
- SHA-256 sidecars accompany both artifacts; exact tools, source and checks are
  recorded in `validation/release-receipt.json` under that output directory.

At this checkpoint, actual-installer lifecycle tests and merged-beta local
installation are pending exit of the user's main and vptest2 Config processes.
No configuration has been overwritten and no running Config process was forced
closed. Setup is unsigned. Clean Windows without Visual Studio and interactive
Config Apply/OK/reopen qualification have not been performed for this build.

### Follow-up completion: RC2 refresh and local deployment

The earlier pending checkpoint is resolved. All 326 real-installer lifecycle
checks, two legacy ZIP-adoption cases and deliberate corrupt-package recovery
passed under an isolated QA identity with process checks scoped to disposable
QA folders. Application payload hashes match the public packages. Standard
Windows PowerShell module paths resolved an initial QA-environment failure;
ZIP cleanup was tested with a matching freshly staged legacy fixture after an
older fixture correctly preserved its differently hashed example configuration.
Failed-run evidence remains in the local validation directory.

At the user's explicit request, the existing [RC2 release](https://github.com/billslack2/videoprocessor/releases/tag/1.3-beta-RC2)
now offers the `6785d0b1092b` Setup and portable ZIP, both SHA-256 sidecars,
`SHA256SUMS.txt`, and updated `release-verification.json`. GitHub asset digests
match local hashes for all six assets. Superseded downloads/sidecars were backed
up and removed. Release notes briefly explain NLS root-profile resolution,
small asymmetric side-inclusion retention and bounded outward/menu fitting.
The existing tag was not moved; notes identify the refreshed source commit and
actual `1.3.005-beta` package version explicitly.

The clean merged-beta payload was deployed to `C:\Videoprocessor\vp` at
17:33 EDT on 2026-09-21: 62 managed files verified, 19 operator-data files
unchanged, no configuration edits. Backup:
`C:\Videoprocessor\vp\deployment-backups\beta-6785d0b1092b-20260921-173310`.
Host and renderer startup logs both confirm clean commit
`6785d0b1092b445a8b05136c2bce1249a09e7d4f`. The release output directory holds
`deployment-receipt.json`, the original RC2 asset/notes backup, final remote
asset metadata, SHA files, and local validation evidence. Unsigned and clean-VM /
interactive configuration qualification limitations above still apply.
