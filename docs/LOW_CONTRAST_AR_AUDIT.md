# Low-contrast aspect detection audit (2026-10-30)

The user supplied a paused Eternals Scope-to-Digital-IMAX failure. Diagnostic input is local at `C:/Videoprocessor/vp/logs/analysis-20261030-220141-20624-1` (97 native v210 frames). Captured pixels must not be committed. Installed source is `dd8e193f`; the investigation worktree started at freshly fetched beta `a8f62855` and preserves that installed history.

## Confirmed findings

| Layer | Evidence | Consequence |
| --- | --- | --- |
| Native boundary measurement | Outer bars are constant code 64. Dark lower picture is commonly code 80; the ordinary black threshold is 88. The real bottom transition is around row 2092, but rough proposals stop around 1718 or 1924 and also invent a left inset around 140. | All 97 unchanged fresh-replay observations remain provisional; reported ratios around 1.99 or 2.24 are incorrect. This reproduces without old geometry, display fitting, or OSD smoothing. |
| Expansion confirmation | Broad-picture evidence uses nonblack occupancy plus brightness/texture. Independent, low-contrast source boundaries do not necessarily make both old excluded strips bright enough. | Correct measured boundaries can still be deferred as vertical-not-broad. |
| Near-black episode ownership | At 21:55:13-19 the log has measured 0,66-3840,2092, confirmed outward proof 3/3, and acquisition blocked. Obsolete-entry release only accepts FULL_RASTER episodes; the submitted-Scope continuity rule restores RETAIN_CROP. | The retained owner can block a real expansion until another scene boundary resets it. |
| Presentation profiles | 16x9 has automatic_crop=false. Scope has automatic_crop=true with configured fill limits. | 16:9 in use while 16x9 is selected is expected; an incorrect detected ratio remains a detector failure. |
| OSD names/deadband | About 1.99 maps to 2.00:1 (Univisium) and a sub-1% display deadband reduces jitter. | The label reflects incorrect upstream measurement; changing labels cannot repair the crop. |

## Candidate correction

Measure both flat-bar boundaries independently from current native pixels when the rough scan fails around a verified bar. Verify distributed abrupt low-contrast picture across four horizontal zones and multiple interior depths, plus the existing strict excluded-bar profile. Preserve uncertain rows and every source column. No standard aspect, screen ratio, inferred opposing boundary, or remembered crop nominates the result.

Carry that explicit native proof through three adjacent-source expansion observations. Keep source-generation, exact-crop, raw-native provenance, repeated-source, and gap checks. Let both retained-crop and full-raster near-black episode owners retire after the existing independent three-sample non-dark proof.

## Wider audit boundaries

Global darkness is range-aware (limited versus full range), but rough-bar thresholds, subtitle flank scans, expansion occupancy, and episode release are separate policies. Native sampling also distinguishes encoded RGB/YUV ranges. These policies need consistent tests with equivalent source signals; a global threshold reduction would trade missed dark picture for false crops on gradients, titles, captions, raised/noisy bars, and limited/full-range inputs.

The candidate is bounded to native opposing vertical bars and their transition handoff. It does not establish that every dark pillarbox, uniformly dim image, raised matte, format, or subtitle path is correct. Existing negative controls and native capture replay are required before claiming a fix. The diagnostic replay reports a successful test even when it only logs observations; explicit expected-boundary assertions are necessary for acceptance.

## Validation of this correction

- 716 selected native regressions passed, including measurement, retention, transition history, decision timelines, sampling/format handling, cancellation budgets, and sparse negative controls.
- All 97 captured frames independently acquire `0,60-3840,2096` (1.88605:1, displayed as 1.90:1 Digital IMAX). The guarded crop retains faint chroma fringe outside the luma boundary around rows 66/2092. It preserves all source columns; neither screen aspect nor a named aspect chooses those edges.
- A production-component replay initialized with logged prior Scope `0,276-3840,1884` retires its retained near-black owner at accepted sequence 7937, after the first available three adjacent native proofs. Capture gaps reset confirmation. This is a component replay, not reconstruction of asynchronous renderer history.
- The original candidate disturbed nine existing one-step boundary-retention controls. Restricting recovery to a materially swallowed picture band preserves those contracts; all nine now pass alongside the actual capture.
- Broad full GPU suites are not repeated under the user's focused-validation instruction. Paired Release build and live source/presentation verification are recorded separately.
