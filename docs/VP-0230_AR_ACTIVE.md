# VP-0230: active boundary trial

This increment follows the Eternals shadow test on `74456bcb`. It is an opt-in active trial, not the final detector replacement. The source base retains the other agent's subtitle and presentation changes through `43a443cc`.

## What changes on screen

With active trial mode enabled by the child-process environment or executable-directory marker, successful new bar measurements and qualified full-picture extent observations replace the existing raw native measurement in the bounded lookahead measurement worker. The renderer imports them only for the exact source, interpretation and context already required by its measurement cache. This can affect detected geometry and eventual framing. Existing transition admission, subtitle inspection/composition and current-source presentation remain responsible for their respective decisions. Existing confirmation delays and reset-related presentation fallback have not been removed.

The scorer keeps several spatial boundary candidates, evaluates opposing pairs jointly and refines the selected pair in native pixels. It combines measured matte variation/darkness, boundary contrast, strip agreement, approximate symmetry, familiar-ratio support and a small continuity bonus. It no longer discards a weaker true edge just because a stronger interior edge exists. One strong edge can support a partly contaminated opposite edge; at least three of nine current strips must support each edge, with at least seven on one edge. History and standard ratios never fabricate a missing boundary or snap coordinates. Arbitrary measured ratios remain valid. A bounded exterior probe near the source sides rejects matte-like panels with distributed visible picture beyond the proposed crop; ratio/history support cannot override that contradiction.

Worker-local nomination history is derived only from independently strong paired measurements. It expires after two seconds or 120 source steps and is discarded on repeated/backward sequence, raster, source-format, interpretation, transport, viewport or renderer changes. A partial observation cannot renew it. No pixels, subtitle templates or absence/cleanup certificates are retained by this history.

The new spatial scan retains its cooperative 2 ms / 24,000-read budget. Prefix statistics now use incremental ordering, reducing repeated sort work. The existing worker already owns the immutable frame; no new worker queue or per-frame foreground scan is introduced. Active mode does not start the separate shadow worker.

Unknown, incomplete, unsupported-range and sparse full-raster hypotheses do not receive new authority. The active trial uses the existing detector on that CURRENT source in those cases. Missing worker results follow existing nonblocking behavior. This is a deliberate incremental replacement, and logs identify which route supplied each sampled result. It does not turn Unknown into detected full raster or blindly retain an old crop.

New evidence does not borrow the old detector's strict matte, side-profile or subtitle certificates. Uncropped axes remain explicitly unknown unless fresh perimeter samples support full extent. Odd boundaries are retained outward to complete chroma pairs: measured coordinates remain separately reported. The existing subtitle path continues to verify current caption/picture preservation independently.

## Launch and assess

Close the currently running VP. Use `Start-AR-Active.cmd` or open `VideoProcessor.exe` in the prepared test folder; its local marker retains active mode across restarts. `Start-AR-Control.cmd` launches the same binaries with the experiment disabled. The deployed player/configuration are not replaced. The test folder has copies of the preceding trial's current cfg/state, preserving operator selections.

Repeat the Eternals scenes, including transitions, dark/dirty bars and visible subtitles. Include seek/rewind/fast-forward, volume UI and subtitle Off mode. Note timestamps for a wrong crop, slow change, caption loss or playback hitch. Logs are in that test folder's `logs\vp.log`.

`[AR-ACTIVE] applied=1` means the new measured geometry supplied the worker observation; it does not claim that downstream presentation applied a crop. `applied=0` means current-frame legacy fallback. Records include exact generation/source/domain/viewport/renderer, measured and selected bounds, score, strip support, history use and worker elapsed time. Correlate with native publication, source-crop, submission and frame-pacing records. Known ratios are confidence support, not physical-screen fitting inputs.

## Validation and remaining limits

`tools\test_experimental_aspect.cmd` covers spatial counterexamples, the bounded shadow worker, the active adapter, exact-source cache import, existing retention/transition admission, chroma-safe rounding, context/expiry handling, missing-edge rejection and current-frame fallback. Run native AR/subtitle regressions and both dependency variants of the subtitle GPU/motion suite before staging.

The previous session identified about five seconds of Unknown and two render-dominated submission gaps; it also had full-input presentation during a fullscreen reset. This change addresses measured-edge ambiguity. It does not claim to fix those rendering/reset incidents. Luma-only sparse sampling can still confuse dark scene structure or large overlays with bars; the initial confidence weights are not calibrated probabilities. Full HDMI critical-path cost, cadence, visual caption preservation and accuracy require the live test. No fresh whole-frame wait, replay, enlarged queue or increased confirmation timeout is introduced.

## Bounded alternative refinement increment - 2026-11-18

The next trial continues the observed c1b130a2 experiment. When the best coarse
pair loses support during native refinement, try at most three ranked pairs per
axis, reusing refined edges. Fixed-size ranking preserves insertion-order ties
without heap allocation. The same 2 ms / 24,000-sample limit covers all attempts;
an incomplete scan remains Unknown and follows current-source legacy fallback.
The exterior-picture veto is unchanged and does not trigger further retries.

AR-ACTIVE records pair_attempts and accepted_pair for horizontal/vertical axes.
An accepted pair numbered above one identifies use of an alternative; applied=1
still means worker measurement, not published or displayed geometry. These
fields add no extra pixel scan and do not supply subtitle or crop certificates.

A synthetic counterexample demonstrates the fixed failure: staggered interior
edges defeat the preferred pair while the retained true 68-pixel bars remain
measurable. The original current trial and scratch retry agreed on 12 older
captured frames and all 64 frames from the November 18 capture. That seek/UI
capture showed IMAX-to-scope detection and admission on the first scope source;
it did not show a delay or a benefit from this correction. Its full-raster
presentation was intentional because automatic crop was off.

The old all-sided-inset admission policy is a separate investigation. This
increment does not change transition admission, subtitle logic, presentation
fallback or selected user profiles. Use the intended Scope profile when judging
visible crop behavior; the saved 16:9 profile disables automatic cropping.
## Positive full-extent trial and restart persistence - 2026-11-18

The paused planet capture is a required regression: actual bright picture
reaches top, bottom and right while the dark left side has no coherent bar
boundary. Mere perimeter brightness is not sufficient full-extent authority.
The qualified path combines structured current picture and competing unpaired
boundary evidence; uncertainty on an edge remains explicit. No aspect snapping,
old pixel reuse, subtitle certificates or extra confirmation delay is introduced.

The detector is active on ordinary startup. Set `VP_AR_EXPERIMENT=off`
for a legacy control run or `VP_AR_EXPERIMENT=shadow` for a diagnostic
comparison. Startup AR-EXPERIMENT records identify the selected mode.

## Current spatial expansion increment - 2026-11-18

The captured scope-to-IMAX change is measured on its first source, but temporal
outward confirmation holds the previous crop. An opt-in current spatial route
combines a completed strong paired boundary measurement with independent,
exact-base/candidate distributed picture evidence in both newly exposed bands.
It applies only to material full-width top-and-bottom expansion that contains
the established picture. Missing, weak, stale or contradictory evidence retains
the ordinary confirmation path. No temporal votes are invented.

Physical geometry and subtitle composition remain separate: publishing current
picture bounds still refreshes the current-pixel presentation retention checks.
Unowned caption pixels outside those bounds must remain visible; qualification
never supplies glyph, cleanup or caption-absence authority. This increment adds
no foreground pixel scan, queue depth, waiting, or forced standard aspect ratio.
Subtitle-profile switching is deferred to VP-0235 and is outside this change.

## Physical measurement and subtitle presentation guard - 2026-11-18

The active trial retains independently strong measured top/bottom geometry when an exact-current subtitle worker proof confirms those same inner boundaries but carries a conservative outer guard. Physical observations keep measured coordinates; the separate guard keeps uncertainty rows visible through presentation and caption-fit ownership. Worker caption evidence, pixel cleanup and current-source identity checks remain independent. Both live and queued adoption use the same selection rule.

This is deliberately limited to exact agreement. Weak, incomplete, mismatched or differently measured boundaries keep the existing subtitle-assisted path. Do not infer improvement for those cases from this correction. The guard cannot authorize a different detected aspect, horizontal crop, stale caption or full-raster absence. An exact matching guard does not force repeated AR acquisition merely because its padded coordinates differ from the published physical picture.

Existing Alpha picture decision diagnostics now include spatial=<reason> for immediate-expansion qualification. This reports existing checks without new pixel sampling or changed thresholds. Camcorder/inset retention and transition timing remain unchanged. Live verification is required before beta integration; the c0d04fc8 checkpoint is preserved.

Integration regression also exposed an uninitialized nominal black reference in the experimental adapter: retention consumed floor 0 / threshold 24 for limited-range input and treated code-64 bars as visible picture. The adapter now provides declared-range nominal values (Limited 64/88; Full 0/24), while leaving calibration and edge/axis qualification flags unset. Invalid or unknown-range sources cannot supply active authority. No new scan or adaptive-matte certificate is introduced. Regression coverage must exercise limited-range retention through final bounded presentation, not merely verify that captions remain visible in a full raster.

## Relative-continuation admission trial - 2026-11-18

This optional route supplements the existing strict expansion proof. For a completed strong current top/bottom measurement expanding an established picture, compare measured picture continuation relative to each edge's matte/noise against coherent evidence for the old boundary. The new score is the least depth-row support across both strips, weighted by the weaker respective boundary support; the old score is the strongest coherent step in the old-edge uncertainty neighborhood. A majority of continuing picture and a strict score advantage can admit expansion when the old every-quarter absolute-brightness rule fails. These are evidence fractions, not calibrated probabilities. Measurements are never snapped to a standard or screen ratio.

The prepared-retention worker reuses 576 existing strip samples and adds at most 960 luma samples for the old-edge neighborhood. The additional proof has its own two-millisecond optional-retention allowance starting at task entry, so ordinary retention consumes that allowance first. This is separate from the detector's two-millisecond budget, not a combined pipeline deadline. The added sampling also checks the remaining sample allowance; subsequent existing retention work is not claimed to fit a universal 24,000-read total. Expiry preserves ordinary retention and leaves the optional proof unavailable.

Only an already completed result matching immutable source storage, frame/context identity, policy, continuity and exact base/candidate may be imported. The renderer does not request, wait for or sample this extra evidence. Missing, expired or stale results follow existing admission. Relative support is not matte calibration, subtitle ownership, cleanup or absence authority. Current-source caption/picture preservation remains downstream. Worker readiness must be assessed in live logs; offline qualification alone does not guarantee first-frame availability.

User does not require AR stability during overlays; that is not a trial acceptance goal. Retained dark-character IMAX frames provide the targeted picture evidence; caption-backed interior cases that fail the conservative comparison stay on ordinary confirmation. Actual captured transition timing and counterfactual old-base sensitivity must be reported separately.
