# VP-0239 Stabilize raised-matte framing on compressed YTTV content

## Status

Backlog

## Evidence

- The pre-YTTV playback pin is `e9918fc53cfeb65a475ddafd337d976126d69838`. The November 21 raised-matte work begins at `6783dff3` and is preserved through `d0c3e301` on `codex/yttv-combined-playback-trial`. Set that series aside after the user reported that the previously working 3 Body Problem subtitle detection failed on the trial.
- The Fidelity ad contains near-symmetric, raised dark mattes against substantially brighter content. The measured picture has been reported around 2.34:1, consistent with known scope framing. Trial playback still lost scope, changed displayed aspect, and flashed. The end card contains real footer text with separate preservation requirements.
- Preserve the November 22 08:26:26 recording and the `VP-Fidelity-known-scope-d0c3e301-x64` playback log, `logs/analysis-20261122-082651-37444-1`, and `matte-incidents`. Correlate recording stamps with final source crop and target, as well as physical boundaries. The captured clips and frames stay local.
- Detector and policy replays passed during the trial while live presentation still failed. Treat competing crop decisions and independently scheduled evidence as hypotheses to trace using the actual selected submission. Isolated detector success establishes only the measurement.

## Work

1. Establish the pre-YTTV core playback baseline and reproduce the 3 Body Problem subtitle cue under matched settings. Capture aspect, original caption pixels, reconstructed glyph onset and continuity, and actual distinct source submissions.
2. Replay the recorded ad through real detector, retention, subtitle and renderer selection order at the captured cadence. Identify the first divergence between measured picture and the selected frame's crop. Include the initial scope lock, moving shots, the flash, and footer onset. Exercise scheduling gaps and out-of-order or unavailable optional results.
3. Use paired boundaries, picture-to-bar contrast and local noise to qualify raised bars. Keep the ratio measured from source pixels. For a robust measurement near a defined ratio, prefer the measured framing through weak near-black variation consistent with compression. Positive evidence of readable text, independent marks or real picture retains protection. Consolidate decisions if multiple stages can contest the same crop.

## Acceptance

- The ad presents steady measured scope through the pre-footer sequence, with no transient full-raster flash or intermediate aspect. Classic preserves readable footer text with picture repositioning. Reconstruction uses original source pixels for glyph recovery and preserves readable text through a current-frame fallback when reconstruction is uncertain.
- Preserve the validated 3 Body Problem subtitle appearance, timing and continuity under matched playback. Include controls for true aspect transitions, dark pictures, interior lines, dense or changing subtitles, profile/format resets and relevant SDR/HDR/Dolby Vision inputs. Verify current-source presentation, NLS integration and bounded live cadence during delayed analysis.
- Require a capture-backed regression that fails on the recorded fault and checks final selected presentation frame by frame, followed by live playback validation on core content and the ad before considering integration.
