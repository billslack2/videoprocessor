# Qualified remembered-boundary shadow comparison

This is an opt-in diagnostic, not a crop behavior change. It addresses the investigation recorded in artifacts/broader-review-20260926-1645/REVIEW.md: scope recovery completed, but a subsequent accepted taller format could not reacquire a dark scope scene. The two existing retention corrections remain intact.

## Operation
Launch with VP_REMEMBERED_EDGE_RETURN=shadow to collect qualified native history and report current-frame comparisons. The existing experimental mode remains separate; the default is off. No configuration file edits or UI settings are required. The diagnostic uses regular logs, samples at most once per second when ordinary analysis runs, and does not force extra primary analysis or change the displayed crop.

Only independent native observations can qualify recent geometry. Existing requirements remain two confirmed scenes with three adjacent native observations per scene, source identity, and bounded age. Held, inferred, repeated and remembered results cannot train history. With inadequate sampling/scene confirmation the diagnostic reports unqualified history; it does not silently lower those requirements.

A current trusted top or bottom edge can nominate a unique historical full-width rectangle despite inward uncertainty in the raw side measurement. Current source pixels then undergo the same strict reference, distributed matched-edge and excluded-band inspection as the existing experimental verifier. The uncertain opposite edge gets additional localized luma/chroma telemetry at the exact nominated coordinate, broken into four horizontal regions. This localized evidence is reported, not converted into a new crop threshold.

The public shadow result contains no observation or publication proof. Shadow nominations are tagged and forbidden at production proof consumers. Collect-only model context cannot authorize remembered publication. The live renderer never substitutes a diagnostic result for native evidence, admission, presentation or queued lookahead.

## Interpreting logs
`Alpha remembered-edge shadow` reports source sequence/epoch, scene and history qualification, current raw geometry, accepted geometry, nominated target, would_verify, excluded-band conflicts, matched-edge support, localized opposite-edge support, presentation owners and elapsed inspection time. `policy_effect=none` identifies its non-authoritative status.

`would_verify=1` means this sampled frame passed the proposed profile comparison. It is NOT a published crop, confidence percentage, calibrated false-positive rate, or completed multi-frame/lookahead proof. Normal preview logs retain the actual primary lookahead decision. Production use requires separately reviewed sequence-bound current/future verification and admission; this diagnostic does not claim that work is complete.

## Validation and limitations
Tests must establish their geometry from synthetic source pixels, then pass through native transition/admission and final crop evaluation. They compare primary and copied preview models with diagnostics enabled/disabled and ensure no change to accepted or displayed geometry. Synthetic fixtures are not recordings of the reported Eternals scene. Same-frame live evidence is still needed to determine whether its exact remembered target passes and whether local opposite-edge evidence is useful.

The diagnostic does not change reset behavior, primary thresholds, global deadbands, retention timing, or analysis cadence. Presentation resets that currently discard model history may require requalification; do not infer a new primary regression from this diagnostic warmup limitation. Source resync continues to invalidate old source facts.

No deployment, merge or release packaging is implied by building this diagnostic. Validation receipts will be recorded under artifacts/remembered-shadow.

## Review findings before live comparison
A targeted test exposed a test expectation error, not an unsafe acceptance: a broad caption produced raw bottom482 outside the remembered bottom472, so the verifier rejected it before pixel sampling. The bright-caption pixels and rejection expectation were retained; the test now proves the earlier geometry veto. A separate narrow-caption case verifies that the dense profile still detects content missed by the broad scan.

A second fixture documents a real limitation: a dark left fifth can leave enough total bottom-edge support but too little support in the first quarter for the existing verifier. The diagnostic reports matched_edge support by quarter without relaxing that guard. Passing the easier positive fixture therefore does not establish that the reported live Eternals frame will pass. This is precisely what the new telemetry must resolve.

Independent architecture review checked production/default mode equivalence and authority isolation. QA reviewed the source-pixel fixtures and discovered the rejection-stage and spatial-support limitations above. No prior test assertions were removed or weakened; only the newly authored broad-caption diagnostic-stage assertion was corrected after its exact output was captured.

## Completed validation
- Clean x64 Release RED build: four intended new tests failed with diagnostic stubs.
- First implementation run: five of six tests passed; broad-caption rejection-stage mismatch investigated with explicit raw geometry and counters.
- Final targeted run: seven of seven passed.
- Full native x64 Release suite: 1,738 passed, zero failed.
- Existing RememberedEdgeReturn test bodies were compared against HEAD and remain unchanged.
- Source and binary SHA256 records, test receipts and independent reviewers are recorded in artifacts/remembered-shadow/validation.json.
- Deployed host, renderer and configuration hashes still match the prior deployment receipt. No merge or deployment performed.

Next test: deploy the verified diagnostic pair with shadow mode enabled, play enough native scope and taller scenes to qualify recent history, then replay the problematic return. Correlate would_verify and matched/opposite regional support with primary preview/final-layout logs. A passing frame still requires separate temporal/lookahead admission work before any production crop change. A failing frame should give a specific current-pixel or history reason; do not loosen thresholds blindly.

## Consecutive queued-window diagnostic (next stage)
The 18:32 replay produced passing single-frame checks at sequences 15414 and 15438, but those were 24 frames apart. They cannot be combined into consecutive confirmation. The new diagnostic checks one fixed qualified nomination against current plus the actually available configured future frames, capped at the existing eight-future-frame maximum. It never waits for frames or carries credit between sampled windows. Current-only windows cannot pass.

Every selected frame must pass the unchanged source-pixel inspector. Metadata is prevalidated for the entire window before pixel work: exact sequence and source-counter continuity, increasing capture timestamps, no cadence repeat or discontinuity, matching transport/format/viewport/renderer/policy identity, and an explicitly declared source generation domain. Native queued source views use format generation; a validated local copy is normalized to transport generation for inspection. Neither the original source nor native evidence is rewritten. A conflicting middle or final frame rejects the whole window.

The renderer samples at most one window per second only in shadow mode, using existing native preview extraction and owned source buffers. It snapshots the live native model without training from future frames, checks the remembered base against current accepted geometry, and applies existing owner/motion/near-black vetoes. Queue membership/timeline continuity and live reference context are rechecked after inspection using separate locks. These are diagnostic snapshots, not atomic publication authorization. No new code writes a scheduled decision, admission evidence, or presentation crop.

`Alpha remembered-edge window shadow` records selected/expected/inspected/passed frame counts, first/last/failing sequence, fixed history and target, pixel failure, elapsed inspection time, and queue/reference validity. `all_pass=1` means the captured consecutive window passed these checks; it is not a motion measurement, calibrated confidence, future publication authority, or proof that the screen would safely switch at an earlier frame. A paused HDMI image can still produce consecutive captured frames. Later activation requires separate review of admission and frame-effective publication.

Current diagnostic sampling and the new window sampling each run at most once per second; elapsed time remains measured because several source inspections cost more than one. Primary detector cadence and all spatial thresholds are unchanged. Tests use synthetic source pixels and cannot establish the reported scene passes this window test until a live replay is collected.

### Queued-window validation completed
- Four new tests failed as intended against a nonaccepting stub after a clean x64 Release rebuild. An earlier incremental build hit LNK1103; it was not counted as a test failure.
- Implemented helper: five targeted tests passed, including an added invalid-nominee/counter-overflow matrix.
- Full native x64 Release suite: 1,743 passed, zero failed.
- All 35 original remembered-edge test methods are unchanged; no existing assertion was weakened.
- Independent architecture and QA reviews found no blocking issue. Scope and limitations above remain applicable.
- Source/binary hashes and test receipts are in artifacts/remembered-window/validation.json. The previously deployed pair and configuration still match the 18:20 deployment receipt; this new queued diagnostic has not been deployed.

Next playback test: deploy the new diagnostic pair when requested, retain shadow mode, then replay the qualified taller-to-scope return. Compare consecutive-window outcomes with the existing single-frame checks and actual final-layout timing. This build gathers evidence; it does not yet activate remembered-geometry cropping.

## Guarded excluded-boundary comparison (tests-first stage)

The 19:22 replay had no passing sampled consecutive window. In a subsequent paused check, all 98 sampled inspections rejected one exterior sample at x2020,y1884: its absolute luma difference from the black reference was six normalized 10-bit levels, against the unchanged tolerance of four. The qualified scope target was y276..1884. This establishes a strict boundary-verification obstacle, not the cause or sign of the pixel difference. The synthetic reproduction uses Y70 against reference Y64; it is not a captured source frame.

The new diagnostic compares the exact target with a separate fixed envelope retaining two additional source rows above and below it, rounded outward to even coordinates (three rows when the nominal coordinate is odd). Learned geometry, reference black and positive matched-edge witnesses remain at their original coordinates. Only the tested excluded region changes. Every still-excluded adjacent row and the deeper bands remain subject to strict profile checks; the legacy chroma-fringe allowance must not move to the new excluded boundary. The envelope must fit within the established taller base.

The exact and guarded windows inspect the same owned captured frames and fixed nomination. Guarded verification cannot select a clean prefix, skip a conflicting future, vary its envelope between frames, train history, or publish crop authority. Both logs include queue/reference validity, tested envelope, retained rows, and first/last/deep boundary conflict details. They explicitly retain policy_effect=none. This adds bounded diagnostic work, whose elapsed time must be checked on live replay; no zero-cost claim is made.

Tests cover the actual 4K boundary coordinates, top chroma and bottom luma, odd/even boundaries, P010/P210/v210, content just beyond the retained strip, narrow captions, insufficient distributed support, true taller content, and conflicting current/middle/final frames. Primary native handoff checks prove the untouched primary path still publishes its exact native target; they do not establish smoothness of a future active guarded-to-native handoff.

This stage remains shadow-only. Passing synthetic tests will make a same-input diagnostic replay reviewable, not establish that remembered cropping is safe to activate or that the live transition has been fixed. No deployment is included in this request.

### Review-discovered sampling gap
The first implemented guarded comparison passed six targeted cases, but QA found that changing the lower excluded-band start also shifted the deeper row cadence. A new 4K test put a six-level luma difference at x2020,y1900, beyond the retained strip. The exact verifier rejected it while the initial guarded implementation incorrectly passed. The new test therefore failed on acceptance, not merely on telemetry. The correction preserves the original nominal grid phase for all still-discarded rows and adds the padded boundary's adjacent checks. This finding was caught before deployment.

The user subsequently authorized deploying the validated diagnostic after tests and independent review pass. Deployment must preserve configuration and replace the verified x64 Release host/renderer pair together; it does not enable crop authority.

### Guarded comparison validation and deployment completed
- Initial clean x64 Release RED: five new tests failed against exact-baseline guarded stubs; the 4K fixture first proved the existing exact mismatch and six-level difference.
- First GREEN attempt had a size_t assertion-type compile error in a new safety test; no tests ran. Only the assertion argument type was corrected.
- Implemented comparison: six tests passed; the new seventh test proved unsafe acceptance from the shifted deeper row cadence. The cadence correction preserved original sampling locations.
- Final targeted run: seven passed. Full native x64 Release suite: 1,750 passed, zero failed. Original 35 remembered-edge methods were unchanged.
- Independent architecture and QA review completed without remaining blockers for shadow-only deployment. Source/test/runtime hashes and exact test receipts are in artifacts/remembered-guard/validation.json.
- User-authorized deployment completed at 19:49:55 EDT. Host and renderer hashes were verified against the tested build and Release/x64 manifests. PID44552 loaded the expected deployed renderer; instance {6D2E3299-7DCA-413A-8C63-D4951BE5C265} emits both exact and guarded schema2 window lines.
- Prior runtime pair, manifests, configuration/state, launcher and log are backed up at C:\Videoprocessor\vp\deployment-backups\remembered-guard-20260926-194954. Configuration edits: none; configuration SHA256 remained unchanged.
- Startup has no qualified history yet following restart. Play clear scope and taller shots, then replay the problematic transition so both comparisons can use the same fresh qualified nomination. Startup logging is verified, but the reported scene has not yet passed the guarded live window; no crop fix is claimed.
