# Rewind scope presentation investigation — 2026-10-14

## Evidence

- Recording: `C:\Users\bslac\Videos\2026-10-14 10-29-14.mp4` (67.283 seconds).
- Preserved playback log: `E:\codex\diagnostics\rewind-scope-20261014\vp-before-capture.log`.
- Live native-input capture: `C:\Videoprocessor\vp\logs\analysis-20261014-103229-58748-1`.
- Source: latest DV2 `15c4c72b`, diagnostics build `ad30e571`.
- Live capture used the diagnostic system-menu command without restarting the paused application.

## Findings and corrections

1. At 10:29:46–49, generation 2, sequences 720–779, analysis accepted the scope rectangle (0,268)-(3840,1892). Presentation still had the taller (0,68)-(3840,2092) contract admitted. The caption-bounded acquisition path checked only whether *any* contract was admitted, disabling a safe picture-plus-caption fit for the new scope contract. Presentation reverted to full raster until the caption disappeared (about 2.48 seconds).

   Use the same exact source-generation, viewport-epoch, raster, geometry, and trusted-axis admission predicate for both presentation admission and caption-fit acquisition. The fit still requires independent current picture authority, a confirmed current caption, and containment of every current visible pixel. Subtitle evidence does not manufacture AR authority.

2. At 10:29:52–55, a top caption reversed a prior bottom-caption translation. The animation was still at +74 pixels while its confirmed new target was -158 pixels. In a near-black frame, the existing guard assumed engagement always starts at zero and rejected the opposite sign, entering a full-raster episode.

   A drift-owned snapshot now certifies the actual start, target, and applied interpolation position. The retention exception requires matching confirmed target and applied displacement, finite values, bounded endpoints, and no overshoot. Existing current-frame, generation, epoch, visible-pixel, admission, scene-boundary, competing-owner, and lifetime checks remain. It cannot acquire scope or recover an already full-raster episode.

## Scope and limitations

These are presentation-state corrections. Detector thresholds, canonical analysis conversion, subtitle glyph rules, and AR confirmation timing are unchanged. No extra image pass or frame allocation is added.

The roughly 191.5 ms wait near the later renderer-generation reset is separate evidence; these fixes are not proof that all reported stutter is resolved. The paused capture can exercise production analysis components, but fresh replay does not reconstruct prior live renderer/history/worker timing. Focused state-transition tests cover the two observed history-dependent failures.

## Validation

- Independent policy review: no actionable findings.
- Clean x64 Release test target built successfully after clearing corrupt incremental linker information.
- Expanded regression suite: 1,399 / 1,399 passed (`TestResults/rewind-scope.trx`), including five new tests. Existing expectations were not changed.
- Native replay: 64 captured frames processed successfully (`TestResults/rewind-native-replay.trx`). The fresh production components found two subtitle lines and acquired (0,268)-(3840,1892); a capture gap resets fresh confirmation as designed. This is analysis evidence, not a live renderer-history replay.
- Full x64 Release solution build: succeeded (`build-rewind-scope-final.log`).
- Initial fixes deployed at 11:05 on 2026-10-14; configuration unchanged. Backup: `C:\Videoprocessor\vp\backup-rewind-scope-20261014-110522`.


## Follow-up: remove intermediate acquisition resize

The deployed replay at 11:07:46 confirmed a single-frame bridge: sequence 3240 displayed the bounded picture-plus-caption fit (0,268)-(3840,2044), then sequence 3241 displayed translated scope with aspect fill (12,268)-(3828,1892). Scope authority was already (0,268)-(3840,1892). This bridge was introduced by the acquisition fix above.

The follow-up completes acquisition and the existing confirmed translation in one presentation decision. The bounded fit still passes current caption/pixel proof, recovery, near-black handling and admission. Before publishing, a small policy helper may resolve the confirmed translation using that same-frame logical admission. It requires exact caption/pixel/frame/base identity, a valid confirmed-owner lifetime, target coverage of the whole validated envelope, and an authentic bounded animation snapshot while engaging. Inspection/fail-open changes made after initial evaluation survive the handoff. Recovery receives no second observation. If any guard rejects, the recovered/admitted safe fit is returned unchanged.

The translation owner can legitimately be from the previous frame: sequence 3239 confirmed +174 before sequence 3240 began engaging at +18. Tests preserve this cadence while rejecting future/expired owners and mismatched animation targets. This follow-up changes no detector thresholds or image analysis passes.

Follow-up validation: clean x64 Release rebuild succeeded; final incremental Release build succeeded after the review correction. All 1,386 selected policy tests passed (`TestResults/atomic-caption-final.trx`), including both-edge atomic handoff and 27 rejection variants. The initial run exposed the future-owner lease hole; explicit nonzero/nonfuture sequence guards fixed it without changing test expectations. Previously run GPU composition tests were not rerun for this CPU-only policy follow-up. Independent review covered the real recorded cadence and integration ordering. Not deployed; live transition behavior still needs user playback validation.

## Follow-up: handoff rejection diagnostics and aligned drift reproduction

The next live replay still displayed fit on sequence 842 and translation on 843 (11:46:42). Existing logs did not record the handoff rejection. Added `Alpha caption acquisition handoff` with exact rejection reason, owner/frame identities, target/raw/routed drift, recovery and individual competing-presentation flags. Failed handoff preserves the admitted fit unchanged.

The renderer-order regression now executes actual drift, `ResolveVerticalBarPresentation`, renderer routing, caption-fit proof, presentation recovery, near-black bounded policy, admission, and atomic handoff. Both signs and late inspection/competing vetoes are covered. This exposed a deterministic rounding defect: a valid raw pose of 16.704 rounds to 18 after chroma alignment, exceeding the old one-pixel comparison. The regression failed with `Expected: accepted; Actual: drift-pose-mismatch` before the correction (`test-handoff-rounding-before.log`).

The atomic helper now runs the raw pose through the same production resolution/routing functions and compares the routed displacement exactly. Interval, finite-value, raster, owner lifetime, frame identity, and visible-pixel checks remain. No tolerance was widened. The separate near-black retention helper was not changed. Independent review found no actionable safety issue. New diagnostics are needed to confirm this is the blocker in the live frame; the test alone does not establish that.

Validation: x64 Release build succeeded; all 1,387 selected policy tests passed (`TestResults/handoff-rounding-fixed.trx`), including the formerly failing rounding regression. Not deployed.


## Follow-up: retain the last rendered crop during caption confirmation

The 12:01–12:03 playback log (`E:\codex\diagnostics\rewind-scope-20261014\vp-rounding-replay.log`) confirms the atomic handoff now succeeds. Remaining jumps are earlier: for example generation 2 sequences 1210–1215 withdraw the prior (0,62)-(3840,2106) presentation to full raster while three subtitle scans confirm the new translation. Sequence 1216 then enters scope. The six-frame interval is approximately 250 ms; timing evidence does not establish a corresponding CPU stall.

A separate presentation-only bridge now holds the last successfully rendered/submitted rectangle during that bounded confirmation interval, including the accepted target's zero-displacement engage frame. Every frame requires current independently trusted native picture geometry, exact-frame measured non-held caption evidence, matching current retention/visible evidence, a non-dark evaluation, and complete containment of the conservative picture-plus-caption envelope. The visible witness cannot extend beyond that envelope. First-observation captions may protect an already displayed superset; the existing two-observation requirement for new crop acquisition is unchanged.

The anchor is separate from logical admission and recovery. Its fixed 500 ms deadline cannot be renewed by repeated frames. Source/format/analysis-domain/viewport changes, source gaps, target or geometry changes, source discontinuities, scene cuts, unknown/dark evidence, competing presentation and visible pixels outside the anchor all reject the bridge. Final geometry bypasses optional fill while held; detector authority, recovery observations, subtitle votes and HDR trusted-picture flags are not established by the hold. Failed render submissions cannot establish an anchor. Existing fallback decisions remain unchanged when rejected.

Architect review requested explicit discontinuity/cut vetoes, now included. Added focused negative/expiry/repeat tests and a composed recorded-cadence regression executing actual confirmation, drift, routing, recovery, near-black bounded policy, admission and atomic handoff for both caption edges. No existing test expectations were relaxed. No new pixel scans, frame copies or image buffers are introduced.

Validation for this bridge: clean x64 Release solution rebuild succeeded (`build-caption-hold-clean.log`); the final Release build with throttled rejection diagnostics also succeeded (`build-caption-hold-ready.log`). All 1,391 selected regression tests passed (`TestResults/caption-hold-final.trx`), including the four new bridge tests. The composed test uses the recorded confirmation cadence and rectangles with a coherent pre-existing fallback episode; it is not a replay of the complete earlier live renderer history. Existing test expectations remain unchanged. Exact post-union visible bounds and caption/anchor eligibility are logged for live acceptance or rejection; repeated unchanged rejections are throttled to two seconds. This bridge has not been deployed or visually validated in live playback.
