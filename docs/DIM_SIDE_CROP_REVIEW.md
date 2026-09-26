# Dim-side inward crop review — 2026-09-24

## Observed delay

The Eternals moving capture at21:30:01 reports independently verified full-width top/bottom bounds0,276–3840,1884 while retaining0,68–3840,2092. Both true bars are flat Y64. Sampled side means are77–81, so the sides differ from the bars but remain below the detector's black cutoff88 and its stronger distributed-picture cutoff112. Both diagnostic side minima are0. Scope publishes at effective sequence726,25 source frames after candidate701 (~1.043s at23.976Hz), with lookahead backdating one frame. The sparse-boundary experiment yields to the native trusted candidate and does not cause this delay.

The first frame is deferred by the possible partial-composition guard; subsequent failed horizontal evidence is retained by the model. Filling skipped side samples does not satisfy the existing distributed-side proof. Lowering its strong cutoff to88 alone still leaves multiple unsupported zones. See artifacts/side-probe-diagnostics/LIVE_FINDINGS.md and its archived log excerpts.

## Why the straightforward bypass is unsafe

Native top/bottom measurement validity and permission to replace an established program format are separate decisions. A full-width vertical crop discards no side pixels but changes magnification. An intentional inset picture with slightly tinted or raised side margins can have genuine top/bottom black bars and a pixel-safe excluded exterior while still needing the existing presentation preserved.

RetainsIncompleteInwardFormat rejects incomplete inward geometry. EvaluateTransitionAdmission separately rejects coherent all-sided partial compositions. Native history and queued publication obey these same restrictions. Removing the model guard alone can enlarge tinted inset compositions; removing both guards also defeats the neutral partial-composition case. Restricting the exception to an established native base, a material symmetric vertical shrink, or a remembered exact aspect does not independently establish program-format intent.

## Decision

Do not deploy a behavior change that bypasses these guards or globally lowers black/detail thresholds. Preserve ordinary lookahead, caption handling, moving-boundary holds, outward expansion, and small-measurement deadbands. The raw-axis comment has been clarified to distinguish measurement authority from established-format admission.

A future evidence-based improvement would require positive spatial/temporal picture structure in the dim source sides, continuous with their interior and distinct from static/tinted/noisy padding. Brightness contrast alone, elapsed time, scene change alone, and the fact that a rectangle appeared in history are insufficient. Any such rule needs adversarial raw-pixel tests with its experimental mode enabled before playback deployment; passing only existing default-mode tests would not validate it. Existing color telemetry explicitly remains diagnostic because tinted noisy bars can match weak color evidence.

For now the captured delay is a conservative detection limitation with a known protective purpose. Further repeated playback of the same clip is unnecessary until a genuinely discriminating rule is available.

## Separate sky/foreground case: a different candidate path

The user's later paused sky/house frame has full-width rough coordinates0,276–3840,1612. The side scan is complete and proposes no sidebars. The bottom scan absorbs dim ground, reporting548 bottom pixels against276 top pixels; both individual edges pass the native edge predicate, but their asymmetry makes the crop provisional.

This is not a reason to relax failed-side admission. A separate diagnostic uses the shallower trusted edge only to nominate balanced0,276–3840,1884 bounds. It independently remeasures both bars with the original thresholds and compares rows immediately inside and outside each nominated boundary. Results are separate from crop evidence and cannot publish a crop.

QA constructed four full-width native-source fixtures with the same rough276..1612 scan: true dim boundary1884, a boundary displaced24pixels lower, a continuous dim ramp, and ordinary asymmetric black bars. Whole-bar contrast alone passes the first three; a local boundary comparison distinguishes the true edge in these fixtures. This proves why symmetry plus existing whole-bar predicates alone would be insufficient. Actual paused-frame measurements are required before considering active publication changes.
