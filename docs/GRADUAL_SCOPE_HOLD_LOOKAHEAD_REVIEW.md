# Gradual scope hold and lookahead review

## Requested behavior

Keep the established scope crop during a positively identified gradual outward format transition, then change framing when the new format settles. This deliberately crops newly revealed edge pixels temporarily and avoids prematurely shrinking the picture into four-sided padding. Abrupt cuts should retain their existing fast handling. The separate caption-return fix must remain intact.

This document records the source audit and the implemented presentation constraints. Independent policy review found no remaining blocker. Exact-scene visual validation remains necessary; synthetic pixel tests cannot substitute for the reported HDMI playback.

## Actual frame budget

`vprenderer/AlphaQueuePolicy.h` selects the current frame plus at most `min(configured lookahead, available future source frames, 8)` future frames. Cadence repeats do not provide additional evidence. Selection does not change queue depth or wait for more frames. A queue containing five distinct source frames has four future frames, because one frame is current.

The configured setting is wired through configuration/profile application and the renderer setter. Changing its value invalidates previously scheduled decision policy; crossing zero also resets partial preview evidence. Increasing lookahead cannot replace missing physical frames or override insufficient current-frame evidence.

## Audited paths

| Path | Existing use of lookahead | Assessment |
| --- | --- | --- |
| Abrupt outward picture change | Current plus two future frames prove broad opposing picture expansion. | Appropriate; deeper analysis is not required for this certificate. |
| Inward picture change | Simulates available adjacent evidence against a copy of the live transition model, bounded by configured/available depth. | Appropriate; live state is not advanced to a future sequence. |
| Caption return | Shared caption inspection runs for preview and live frames; current pixels and protected bounds are revalidated. | Preserve this separate fix; cache only original extraction evidence. |
| Translation-to-picture handoff | Dedicated queued outward certificate with current ownership and geometry checks. | Appropriate narrow exception. |
| Subtitle FIT ownership | Buffered expansion is inspected diagnostically but does not transfer ownership. | Deliberate safety exclusion. |
| Active gradual edge movement | Queued publication cannot bypass active motion. | Required for stable framing during a ramp. |
| Gradual endpoint | Approximately 250 ms of adjacent quiet evidence is currently evaluated on live frames. | Deliberate limitation described below. |
| Darkness or ambiguous evidence | Future evidence cannot make an unproven current frame safe to crop. | Preserve current safeguards. |
| Startup acquisition | Initial crop cannot be backdated from implicitly assumed full raster. | Appropriate conservative acquisition. |
| Queue drops and context changes | Continuity, source, renderer, viewport, policy, and exact effective-frame identity are checked. | No blocking defect identified in this audit. |
| Native versus converted input | Preview reads native capture samples before renderer conversion; final consumption validates the live analysis evidence. | Lookahead is available with both P010 conversion and the native/P210 rendering path. |

## Settlement optimization boundary

The current moving-state quiet-settlement test is live-only. Lookahead therefore does not remove all endpoint dwell. A later optimization could simulate the moving state and admission rules across queued physical frames and issue an exact-current-frame settlement certificate. It would need independent continuity, current-pixel, owner, and publication validation.

Do not substitute the ordinary three-frame outward certificate for this settlement proof or allow it to bypass active movement. A short quiet portion of a ramp is not necessarily the completed transition. Keep this possible optimization separate from restoring the requested scope hold.

## Source areas reviewed

Paths below are relative to `src/VideoProcessor-Lib`:

- `vprenderer/AlphaQueuePolicy.h`: physical availability and preview selection.
- `vprenderer/LibplaceboVideoRenderer.cpp`: configuration invalidation, native preview inspection, caption inspection, queued proof construction, live ownership/admission, and final runtime identity validation.
- `vprenderer/BufferedPictureExpansion.cpp`: moving-state continuity and settlement; outward and inward certificate construction/validation.
- `ActivePictureDecisionTimeline.cpp`: pending-frame association, consumed/discarded frames, continuity boundaries, and policy generations.
- `ActivePictureTransitionModel.cpp`: live model authority and confirmation cadence.

## Validation to retain

Existing coverage includes configured depths 0/1/2/3/5/8, short queues, repeat exclusion, source/timestamp gaps, policy changes, stale identities, preventing publication before the changed source frame, inward/outward round trips, hard cuts during movement, and native-v210/P010 caption parity.

Run the relevant `AlphaQueuePolicyTests`, `ActivePictureDecisionTimelineTests`, `LookaheadOutwardProofTests`, `MovingPictureTransitionTests`, and `InwardCaptionEvidenceTests` with the new gradual-hold regressions. New tests should explicitly require retaining established scope during proved movement, settling once, preserving hard-cut handling, and not bypassing current caption protection or context-reset safeguards.

## Review outcome

The correction retains presentation only after the existing motion detector has confirmed the episode and the crop was previously admitted. It preserves exact geometry, configured fill and NLS mapping, while recovery and explicit safety owners retain precedence. The ordinary lookahead/caption certificates and adoption rules were not broadened. Review checked the renderer path as well as the pure crop-policy boundary.

The regression baseline produced seven expected assertion failures before implementation. The new endpoint fixture initially demanded literal zero-height bars; source measurements showed the unchanged detector already reports `FULL_RASTER_TRUSTED` with eight pixels remaining. Its corrected invariant is to retain scope until affirmative full-raster authority, then remain full frame. The fill assertion compares the original displayed bounds, including configured horizontal trimming. Neither correction changes production thresholds or disables fill.

Focused validation: clean x64 Release test-target rebuild succeeded; all 69 tests passed across the movement, handoff, buffered proof and inward-caption suites. Full-solution build and full-suite results are recorded separately in artifacts/gradual-hold-validation.json after completion. No deployment or configuration changes are included.

## 2026-10-15: advanced subtitle ownership during gradual expansion

The 15:47:37 recording and runtime log exposed an ownership conflict before the gradual hold could engage. Classic subtitle translation/drift vetoed motion observation in an advanced subtitle mode. An intermediate physical crop was then published and rejected by final visible-pixel admission; the output became full raster before motion tracking recovered against a later, wider base.

The correction keeps the legacy motion detector and its current-pixel evidence requirements. A mode-aware input copy removes suppressed Classic translation state only for advanced picture placement. Real FIT and FAIL_OPEN owners remain competitors. The final crop route removes translation vetoes only when the existing exact-current moving-hold predicate succeeds, and final admission still requires the same previously admitted physical Scope base.

Advanced subtitle picture placement also returns to the beta contract: uncertain, pending, blank and composed scans do not select different picture poses. This does not prove caption absence or mark unprocessed pixels clean. A missed/unprepared caption outside the physical crop remains a separate detection/composition issue; automatic Classic fallback is not used to conceal it.

New regression coverage combines pre-existing translation/drift, alternating advanced composition availability, current visible pixels outside the old Scope bounds, final admission and configured aspect fill. Separate negatives retain Classic, FIT, full-authority, stale identity and missing-prior-admission behavior. Build/test evidence for this revision is kept in the diagnostic build logs; live HDMI replay is still required before claiming the recording's entire sequence is fixed.

The follow-up recording `2026-10-15 16-06-03.mp4` shows the source in a fixed 16:9 viewport. Extracted samples at 5, 8, 11 and 14 seconds show the letterbox bars retreating gradually toward full height. This supports treating the episode as a moving-boundary transition, rather than interpreting each intermediate height as an abrupt new aspect ratio. This is rendered recording evidence, not a native analysis-frame fixture.

## 2026-10-15: replay exposed a scene-boundary reset during the ramp

The main fullscreen replay at 16:19:30–16:19:47 logged subtitle processing mode 0, with temporal mixing off. Mode 0 alone does not distinguish Classic from Off. The established Scope hold became active at source 377 and retained the original 270–1884 picture bounds through source 431. At source 432, a near-black-entry scene notification (no pending or confirmed hard cut) cleared the moving-picture state before current analysis. Source 435 then published an intermediate 156–2004 crop. Later dense FIT scans treated newly revealed picture as bar content, producing further small crop changes. Full-raster authority arrived at source 520.

The correction carries a prior active episode through that notification only after current native pixels corroborate it: exact adjacent identity and context, the same established base, small continuing outward movement, non-dark AR evidence, and the existing broad or native-distributed opposing-edge proof. It does not create a new crop, excuse a hard-cut candidate, retain across a frame gap, or promote subtitle evidence into picture authority. Quiet/settlement counters restart at the boundary. Existing final admission and full-raster handling remain in force.

An additional FIT override was considered and rejected. The renderer already suppresses dense bar-content scanning while an exact-current gradual hold is active; preserving the episode keeps that existing protection working. The fix therefore adds no new FIT ownership rule and no pixel thresholds. Boundary telemetry records whether the prior episode was retained.

The architectural contract remains: physical AR uses current picture/bar evidence and independently confirmed subtitle occlusion information; selected subtitle presentation must not erase established physical AR. Unknown or obscured evidence is not affirmative full-raster evidence. Existing dense Classic caption movement is suppressed during an established gradual ramp; this patch preserves that existing limitation rather than changing subtitle-mode semantics.

Validation for this follow-up is recorded after the new boundary and lifecycle regression tests complete. Live replay remains necessary; the logs identify the failure mechanism but are not a pixel-perfect native replay fixture.

A second regression test reproduced stale motion-edge bookkeeping through a prolonged native-distributed (dark-picture) evidence interval: the episode stayed active, but its last edge coordinates did not advance. The subsequent cumulative displacement could trigger the ordinary abrupt-change exit. The correction updates those coordinates only for an already active episode with adjacent exact-current native-distributed proof. Such samples cannot start a motion episode; an already established episode uses the same anchored quiet-settlement rule as bright-picture evidence, so a stationary dark endpoint can finish normally. The new test failed before this correction; missing proof, gaps and real abrupt changes retain their existing behavior.

Follow-up validation: x64 Release full rebuild and final incremental solution build completed successfully. The 22 focused boundary/moving-picture tests passed, followed by 1,335/1,335 broader AR, subtitle, motion, analysis and DV regression tests. The native-distributed continuation regression was observed failing before its correction. Logs: vp-gradual-boundary-build.log, vp-gradual-boundary-settlement-build.log, vp-gradual-continuation-red.log, vp-gradual-boundary-settlement-focused.log and vp-gradual-boundary-regressions.log in the local temporary directory. Independent final review found no remaining blocker. This candidate was subsequently deployed at 16:48:30 (backup: C:\Videoprocessor\vp\deploy-backups\gradual-boundary-20261015-164748). The next replay exposed the remaining uncertified-position problem below.

## 2026-10-15: subtitles-Off replay isolates stale native position

The user requested subtitles be turned off and the remaining AR stutters investigated independently. The live profile was already Off when read through the supported configuration RPC; selecting Off again confirmed it without editing configuration files or restarting VP. The runtime profile change at 16:50:05 records box=0 and move=0; subsequent subtitle worker counters show zero scans. This is stronger mode evidence than the rendering mode=0 field, which also covers Classic.

In the 16:50:34-16:50:41 replay, the original Scope bounds 0,268-3840,1884 survive motion acquisition at source 2277 and the darkness notification at 2353. Native edge measurements continue moving through source 2363 (106-2056), while both the broad and native-distributed expansion-strip proofs are unavailable. The tracker advances its source identity but leaves its certified edge position frozen. At 2366 (100-2060), accumulated displacement is mistaken for a one-frame abrupt jump, releasing the hold. Intermediate heights are then published at 2370 and 2390, before actual full-height evidence arrives at 2419. This explains the visible stepping despite the preceding scene-boundary correction.

The correction stores a separate immediately observed native edge position and source stamp. Only adjacent current native trusted vertical bars with matching measured/proposed geometry can populate it. Missing native scans, source gaps and context changes invalidate adjacency. These coordinates never acquire a moving episode, grant crop authority, advance certified geometry, or earn quiet endpoint votes. Abrupt-jump comparison uses adjacent observed positions; settlement still needs existing strong pixel proof. The darkness-boundary helper uses the same exact predecessor rather than comparing against stale certified geometry. Its current native/strip proof and hard-cut vetoes are unchanged.

Transition-only diagnostics now report acquisition/exit reason, prior observed sequence and rectangle, prior certified rectangle, current rectangle, and quiet count. No additional image scan, worker, wait, or frame retention is introduced. The replay has matching selected/source identities and no temporal mixing; its analysis telemetry reports no source-period overruns. Optional lookahead exceeds its small 1 ms allowance during part of the ramp (roughly 1.3-3.0 ms); that remains separately observable and is not evidence of a missed 41.7 ms source deadline or an explanation for the explicit wrong crop publications.

The earlier 16:48 replay also showed translation and one crop change before the motion episode was acquired. The later verified-Off replay retained the original base during acquisition, so startup/translation thresholds are not changed based on that earlier mixed-mode sequence.

Validation is recorded below after completion. These regression fixtures reproduce tracker evidence and final crop admission, including a real SceneDetector darkness event; they are not a pixel-for-pixel replay of the HDMI source. A live replay is still required to confirm the entire visible transition.
Validation completed: restoring the previous stale-edge comparisons caused five of the seven new native-position tests to fail; the corrected comparisons passed. The x64 Release solution build completed, followed by 49/49 focused moving-picture, darkness-boundary and handoff tests and 1,342/1,342 broader AR, subtitle, analysis, motion and DV regression tests. Three older handoff tests initially failed because their synthetic frames omitted completed native vertical-scan metadata; adding that production metadata fixed them without changing any assertion or production rule. Independent reviews found no remaining blocker in the new continuity logic. Logs: vp-native-position-red-tests.log, vp-native-position-build.log, vp-native-position-final-focused.log, and vp-native-position-final-regressions.log in the local temporary directory. The first incremental test link encountered corrupt generated debug information; recompiling generated test intermediates resolved the build issue.

The new code is built and tested but has not been deployed. Live subtitles remain Off; this work did not edit configuration files. The actual HDMI transition remains a required visual check after deployment, so these results do not claim the whole scene is visually verified.

Deployment: installed the verified x64 Release host and both renderer DLLs at 17:20:54 on 2026-10-15. Backup: C:\Videoprocessor\vp\deploy-backups\native-position-20261015-172054. VP restarted as PID 52436 and loaded both deployed renderer paths. Its saved default selected Classic on startup; the supported live profile RPC selected Off again as requested. Active configuration file hash remained unchanged; no configuration file edits were made. Live replay of the complete transition is still pending user evaluation.

## 2026-10-15: successful replay and next optimization work

The user reported that the deployed candidate looked good. In the 17:21:20-17:21:29 fullscreen replay, the established Scope rectangle remained stable through the gradual expansion, followed by one full-height change at source 625. All 44 logged source/selected identity samples matched. Subtitle processing was inactive in this replay; this acceptance does not establish advanced-subtitle performance or universal transition coverage.

Existing telemetry for this replay reports approximately 10.4 ms average active presentation-path work and a 16.0 ms peak, with no source-period overruns. These are wall-time measurements, not isolated CPU execution times. Optional synchronous lookahead reached 3.53 ms and sometimes exceeded its separate 1-2 ms allowance. Background worker cost is separate, and CPU/GPU timings must not be added as if they were sequential stages.

Next work is performance optimization, preserving this tested behavior. The review identified these priorities; none of these new proposals is implemented by this check-in:

1. Remove duplicate luma/chroma sampling and conversion at the same coordinate, duplicate percentile calculations, and unnecessary temporary allocations.
2. Add exact SIMD row/block analysis where beneficial, preserving scalar thresholds, rounding, chroma footprints, first-failure semantics and scalar fallback. DV unpacking and several format conversions already have AVX2 paths.
3. Share immutable per-frame raw AR and darkness measurements between normal lookahead and outward-window proof workers. Reuse sampled subtitle pixels within the same source frame where parity is established.
4. Dispatch independent analysis earlier and evaluate a small bounded worker pool for buffered frames or independent scan regions. Keep source storage owned until jobs finish, prioritize near-deadline frames, reject stale frame/domain/policy results, and preserve ordered AR/cue tracking and publication. The existing subtitle scanner carries predecessor state and cannot simply be run concurrently across frames.
5. Review synchronous lookahead allowance, deadline granularity and remaining proof work against actual presentation deadlines. Prefer completing expensive measurements ahead of presentation and consuming ready results cheaply. Rendering must not wait for optional analysis or retain source frames longer to obtain it.

Retain current sample-range interpretation, color coefficients, bit-depth rounding, native boundary checks and source-frame transition rules. Subtitle discovery already uses roughly a 960x540 sampling grid with native-resolution refinements. Further resolution reduction, alternate-frame detection, or full-frame format conversion requires separate quality and performance evidence.

Evaluate optimizations independently using exact analysis-result parity, the existing AR/subtitle/motion/DV regressions, relevant recorded transitions, and average/tail timings, worker queue waits, contention, deadline misses and stale-result counts. Higher core utilization alone is not a success criterion. SIMD, parallelism and scheduling work must preserve small glyphs, dark picture evidence, subtitle occlusion handling and gradual Scope retention.

This check-in retains the previously completed x64 Release build and 49/49 focused plus 1,342/1,342 broader passing regression evidence. Source files were compared with the deployed source snapshot before committing; only documentation was added after that validation. No tests or performance benchmarks were rerun for this documentation/check-in step.
