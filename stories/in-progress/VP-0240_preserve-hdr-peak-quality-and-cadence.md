# VP-0240 Preserve HDR tone mapping while reducing peak analysis cost

## Status


In Progress

The opt-in x64 Release comparison is staged locally. The source branch `trial/vp0240-libplacebo-histogram` in `E:\codex\videoprocessor\vp0240-hdr-histogram-20261125` starts at verified `v2.0.10-beta` tip `d5f69645f4c818559d627d68eced177489c159d5`. The original detector reference and delayed mode are at `48aa98fdb65ccbfb5c25c2c6b04b8f44abda4d35` with bundled libplacebo `a89f8241bfd6c5f5affaa9c3ce39fa2b1767b8c7`. The direct-render half-rate prototype is at `987fdc0cf95add3266f699234462a7065ca5d2ac` with the local libplacebo fork commit `33f88fe42b0f9f62ab13e12fbe2fce616c868dd9`.

## Review and decision

Astra verified that `allow_delayed` can still force D3D11 readback and that disabling per-draw peak parameters resets the measured state. The first trial compares both native modes with the shipped DLL. The second uses one staging copy and a nonflushing readiness query with nonblocking map. It preserves the native histogram, ROI, percentile, smoothing in elapsed source frames, and per-field authored Dolby Vision precedence. It attempts every source through 30 fps and alternate sources above 30 fps. The bounded result age is one source frame at full cadence or two at half cadence. The renderer uses native authored or static handling when a compatible measurement is unavailable. Direct progressive rendering is the half-cadence scope. Smooth Motion and interlaced temporal passes use the native reference until per-packet source scheduling is added.

Keep the half-resolution advanced-subtitle variant gated on full-resolution timing and image evidence. Linear-light 2x2 downscale can attenuate small HDR highlights and can increase result age when staged. A fused reduction that retains each sample's original contribution would preserve the native distribution but needs separate timing qualification. Record worst-case stage and full critical-path timing, distinct source presentation gaps and producing/consuming source IDs before promoting any change.

## Trial evidence

- Both VP builds succeeded as x64 Release with pinned MSVC 14.44.35207. The staged Beta plugin API 34 and paired libplacebo DLL loaded for each build. `BUILD-IDENTITY.json` in each trial folder records the payload hashes.
- All 15 libplacebo Release tests passed, including the D3D11 fixture for histogram parity, half and full cadence, cut and ROI reset, and stale-result expiry.
- The 31 focused VP native HDR, dependency and Smooth Motion regressions passed on each isolated libplacebo DLL. Broader native suite runs did not finish in this qualification. The earlier beta baseline had 23 already reproduced failures (22 AR and one LUT), so there is no new full-suite result or claim of clearing those cases.
- The matching live HDMI comparison, advanced-subtitle half-resolution experiment, and worst-case render and presentation measurements remain open. The active install was untouched. The isolated launchers and verification artifacts are under `E:\codex\videoprocessor\vp0240-hdr-histogram-20261125\artifacts`.

## Advanced subtitles at 23.976/24 fps

The user confirmed that the performance problem also occurs at 24 fps. The new opt-in x64 Release trial on `trial/vp0240-advanced-subtitle-histogram` at `3feabef84145bd76504f2fe8ebfb77fce3455488` uses fork `0165c292419cff816c39493053a6583b149da3f5`. It measures alternate distinct direct HDR sources when an advanced subtitle mode is active, including at 23.976/24 fps, and retains the native full-density histogram on measurement frames. The matched control attempts every direct source at 24 fps. Both modes reuse completed D3D11 staging resources and use authored or static data if compatible measurements are unavailable. Active temporal mixing still uses the native path and remains open for separate source-keyed optimization.

The separate fused 2x2 linear-light experiment passed ROI and tile-statistic checks. In a warmed synthetic 4K run, its 0.876 ms mean and 1.277 ms worst were no improvement over 0.865 ms mean and 0.899 ms worst for full density, so the trial uses full density. With reusable readback, alternate cadence issued 12 histograms across 24 synthetic 4K renders, versus 24 from the full cadence control. Both had a compatible measurement for all 24 renders. Mean end-to-end off-screen timing was 0.736 vs 0.814 ms and worst was 0.891 vs 0.998 ms. The timing includes a GPU finish in the fixture and is separate from actual HDMI or subtitle compositing cost. All 15 fork Release tests and 31 focused VP tests passed. The staged launcher pair and SHA-256 manifest are under `E:\codex\videoprocessor\vp0240-hdr-histogram-20261125\artifacts\advanced-subtitles-3feabef84145`. The native HDMI test with dense advanced subtitles and actual presentation gaps remains open.


## Context

- The 2.0 beta compact HDR path replaces libplacebo peak detection for eligible inputs. Its sparse grid and cadence change the peak estimate, and disabling the detector can bypass profile tuning, mastering-peak soft clipping, and measured Dolby Vision metadata fill-in.
- The user accepts one frame of analysis lag. Trial a full-pixel histogram at half source cadence above 30 fps, then test a quality GPU downscale to half width and height with advanced subtitles if more headroom is needed. A two-frame pipeline can prepare the analysis texture on one frame and measure it on the next.

## Work

1. Get an Astra principal-engineer review of scheduling, alternatives, state reuse, HDR reduction quality, and the live frame budget. Record the decision and alternatives.
2. Build an opt-in x64 Release comparison trial from the current official beta. Keep the configured libplacebo histogram, tuning, processing region, soft clipping and Dolby Vision fill-in where applicable.
3. Compare full-rate, half-rate, and staged half-resolution analysis on matched HDMI inputs with moving highlights, cuts, fades, HDR10, HLG, Dolby Vision and dense advanced subtitles.

## Acceptance

- No optional HDR work blocks current-source video. Measure the worst scale-frame and histogram-frame critical paths, source presentation gaps and result age.
- On a verified cut discard incompatible pending work and prioritize the new scene. Keep analysis bound to the source, format and target context.
- Provide an isolated test build, exact source identity and rollback guidance. Record visual and timing gaps before recommending a release default.
