# VP-0189: Stabilize automatic crop and diagnose dark-scene transitions

## Status

Review

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
