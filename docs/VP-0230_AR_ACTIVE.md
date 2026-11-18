# VP-0230: active boundary trial

This increment follows the Eternals shadow test on `74456bcb`. It is an opt-in active trial, not the final detector replacement. The source base retains the other agent's subtitle and presentation changes through `43a443cc`.

## What changes on screen

With child-process environment `VP_AR_EXPERIMENT=active`, successful new bar measurements replace the existing raw native measurement in the bounded lookahead measurement worker. The renderer imports them only for the exact source, interpretation and context already required by its measurement cache. This can affect detected geometry and eventual framing. Existing transition admission, subtitle inspection/composition and current-source presentation remain responsible for their respective decisions. Existing confirmation delays and reset-related presentation fallback have not been removed.

The scorer keeps several spatial boundary candidates, evaluates opposing pairs jointly and refines the selected pair in native pixels. It combines measured matte variation/darkness, boundary contrast, strip agreement, approximate symmetry, familiar-ratio support and a small continuity bonus. It no longer discards a weaker true edge just because a stronger interior edge exists. One strong edge can support a partly contaminated opposite edge; at least three of nine current strips must support each edge, with at least seven on one edge. History and standard ratios never fabricate a missing boundary or snap coordinates. Arbitrary measured ratios remain valid.

Worker-local nomination history is derived only from independently strong paired measurements. It expires after two seconds or 120 source steps and is discarded on repeated/backward sequence, raster, source-format, interpretation, transport, viewport or renderer changes. A partial observation cannot renew it. No pixels, subtitle templates or absence/cleanup certificates are retained by this history.

The new spatial scan retains its cooperative 2 ms / 24,000-read budget. Prefix statistics now use incremental ordering, reducing repeated sort work. The existing worker already owns the immutable frame; no new worker queue or per-frame foreground scan is introduced. Active mode does not start the separate shadow worker.

Unknown, incomplete, unsupported-range and sparse full-raster hypotheses do not receive new authority. The active trial uses the existing detector on that CURRENT source in those cases. Missing worker results follow existing nonblocking behavior. This is a deliberate incremental replacement, and logs identify which route supplied each sampled result. It does not turn Unknown into detected full raster or blindly retain an old crop.

New evidence does not borrow the old detector's strict matte, side-profile or subtitle certificates. Uncropped axes remain explicitly unknown unless fresh perimeter samples support full extent. Odd boundaries are retained outward to complete chroma pairs: measured coordinates remain separately reported. The existing subtitle path continues to verify current caption/picture preservation independently.

## Launch and assess

Close the currently running VP. Use `Start-AR-Active.cmd` in the prepared test folder. `Start-AR-Control.cmd` launches the same binaries with the experiment disabled. The deployed player/configuration are not replaced. The test folder has copies of the first trial's current cfg/state, preserving operator selections.

Repeat the Eternals scenes, including transitions, dark/dirty bars and visible subtitles. Include seek/rewind/fast-forward, volume UI and subtitle Off mode. Note timestamps for a wrong crop, slow change, caption loss or playback hitch. Logs are in that test folder's `logs\vp.log`.

`[AR-ACTIVE] applied=1` means the new measured geometry supplied the worker observation; it does not claim that downstream presentation applied a crop. `applied=0` means current-frame legacy fallback. Records include exact generation/source/domain/viewport/renderer, measured and selected bounds, score, strip support, history use and worker elapsed time. Correlate with native publication, source-crop, submission and frame-pacing records. Known ratios are confidence support, not physical-screen fitting inputs.

## Validation and remaining limits

`tools\test_experimental_aspect.cmd` covers spatial counterexamples, the bounded shadow worker, the active adapter, exact-source cache import, existing retention/transition admission, chroma-safe rounding, context/expiry handling, missing-edge rejection and current-frame fallback. Run native AR/subtitle regressions and both dependency variants of the subtitle GPU/motion suite before staging.

The previous session identified about five seconds of Unknown and two render-dominated submission gaps; it also had full-input presentation during a fullscreen reset. This change addresses measured-edge ambiguity. It does not claim to fix those rendering/reset incidents. Luma-only sparse sampling can still confuse dark scene structure or large overlays with bars; the initial confidence weights are not calibrated probabilities. Full HDMI critical-path cost, cadence, visual caption preservation and accuracy require the live test. No fresh whole-frame wait, replay, enlarged queue or increased confirmation timeout is introduced.
