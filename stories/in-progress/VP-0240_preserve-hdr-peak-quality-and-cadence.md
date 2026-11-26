# VP-0240 Preserve HDR tone mapping while reducing peak analysis cost

## Status


In Progress

The requested native HDR restoration is in progress on source branch `codex/v2011-native-hdr`, worktree `E:\codex\videoprocessor\v2011-native-hdr-20261125`, based on verified beta `v2.0.10-beta` at `d5f69645f4c818559d627d68eced177489c159d5`. The compact HDR peak replacement has been removed and native libplacebo peak detection restored at source commit `24635341653800ce78520e51b77741abf4e072f0`, which was pushed as the official `v2.0.11-beta` branch. The pinned x64 Release all-phase build and subsequent incremental build succeeded, and all 48 focused HDR tests passed. The broader Smooth Motion suite stalled after approximately 90 successful cases and did not complete. Installer and portable packages were built locally, but formal export is blocked because the `vp-release` skill `build-vp.ps1` invokes the nonexistent `x64/Release/VideoProcessor-Test.dll`; the actual test binary is `x64/Release/tests-beta/VideoProcessor-TestBeta.dll`. The full beta test suite was stopped after it ceased making progress at the same point as the broad Smooth Motion suite, so it has no completed result. GitHub default remains `v2.0.10-beta`; automatic approval review rejected changing the default to `v2.0.11-beta` without explicit user authorization. The isolated `artifacts/2.0.11-beta-local-test` payload includes same-source Config, which was verified with an offline screenshot, and isolated copies of user configuration and state. After adding the output probe and its runtime record, its 115 payload hashes matched. Its copied shader cache has SHA-256 `1B2F6EA52EBC96B54F4AAC83BE8ACCB4F1F781798A70B3925652E17A87DFAE54`. Live playback and formal release verification remain pending. The focused Smooth Motion and subtitle optimization proposal is being prepared at `C:\Users\bslac\Documents\ChatGPT\Madve\VP-0240-smooth-motion-proposal.md` for later review.

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


## Smooth Motion and advanced-subtitle interaction

The matched 23.976 fps Dolby Vision recording with Smooth Motion Auto showed advanced captions dim, return and disappear over successive 59.95 Hz output frames. The paused OSD reported a 16.68 ms output budget, new-source CPU/wait of 20.77 ms average and 27.30 ms maximum, reuse of 0.37 ms average and 1.16 ms maximum, and GPU work of 5.26 ms average and 19.17 ms maximum. The run log reported `temporal=1` with the direct HDR trial inactive. The faster average included many reuse outputs and left the distinct-source spikes visible.

Astra agreed to both changes and reviewed the corrected candidate at `063d3f2155aa6f53487053f0a3826989e86e95e2`. The progressive test path supplies the existing native histogram with the actual owned source sequence and a one-source-frame nonblocking readback window. It preserves authored precedence and skips measurements whose saved ROI belongs to a different crop. Smooth Motion mixes compatible caption-clean pictures and draws one verified cue from the floor output timestamp. A source with pending evidence keeps its original pixels and uses a wider fit when the existing crop lacks pixel proof. Unsupported presentation modes stay on their existing paths.

The local branch is `trial/vp0240-temporal-hdr-cues` under `E:\codex\videoprocessor\vp0240-temporal-hdr-cues-20261125`. `artifacts\trial-063d3f2155aa` contains the opt-in and reference launchers, source patches, the paired API 388 DLL (`0165c292419cff816c39493053a6583b149da3f5`), Config, the output probe, and a SHA-256 manifest. Both runtime and Config were built with pinned MSVC 14.44.35207 x64 Release. All 132 payload hashes matched and Config started offline from its staged location. All 34 focused native checks passed against the exact DLL, including packet scheduling/reset, discrete cue selection and current-source fallback. The new scheduling test uses a mock readback callback.

The separately launched broad subtitle/motion suite with the bundled dependency recorded 90 passing cases before output stalled. The run was stopped and has no completed suite result.

Keep the story in progress for the matched HDMI comparison. The live 16.68 ms budget, worst source and presentation gaps, maximum measurement age, HDR glyph brightness and edge scaling, small highlights, and the previous Dolby Vision caption flicker remain unverified. The windowed telemetry records the latest eligible and measured packet IDs and counters. The active installation was untouched.

## Scope framing regression in the first Smooth Motion trial

The user reported scope shrinking with bars on all four sides in `063d3f2155aa`. In its 14:03:59 and 14:04:15 submitted-geometry records, the selected packet had already admitted crop `10,266–3830,1892`. The trial replaced it with the full `3840×2160` capture raster and reduced output width from approximately 1,999 to 1,513 pixels. Both were mode 3 with a successfully composed subtitle. A guard treated the padded subtitle layer as a reason to refit the entire capture. Encoded top and bottom bars then remained inside the narrower picture.

`2a16d013b3a6577376010f5eff282470b26e4a24` removes the post-admission framing override. Clean and pending packets use the existing viewport mapping. The cue layer clips to that mapping, and an attempted cue overlay can fall back to the existing selected-packet composition. Astra agreed to this scope. A focused GPU case exercises a known scope crop with a padded layer extending outside it and checks both picture edges and the selected visible glyph. The corrected x64 Release package under `artifacts\trial-2a16d013b3a6` supersedes the first trial and includes matching Config, output probe, and exact hashes. All 56 focused tests passed with the exact trial DLL. All 133 package hashes matched and Config started offline.

The failed run's diagnostic capture was metadata-only. The exact HDMI sequence, a pending raw caption wholly below the admitted crop, and actual presentation timing still require live verification. The failed run recorded zero temporal histogram submissions and is insufficient to evaluate HDR performance. The active installation was untouched.

## Restart from the Smooth Motion baseline

The user reported an unacceptable AR transition during the combined follow-up and requested a return to the starting point with Smooth Motion isolated. The next test uses the verified official `v2.0.10-beta` tip `d5f69645f4c818559d627d68eced177489c159d5` and its bundled libplacebo `a89f8241bfd6c5f5affaa9c3ce39fa2b1767b8c7`. The prior HDR histogram and caption trials are paused. No active deployment was changed.

The isolated `E:\codex\videoprocessor\vp0240-smooth-motion-baseline-d5f69645\artifacts\Smooth Motion Only` configuration keeps the previous Scope viewport and Smooth Motion Auto. The two settings changes are subtitles Off and peak detection Off for the current Low Cinema Filter 16x9 profile. HDR tone mapping remains enabled. The package includes same-source x64 Release Config and probe. All 125 package hashes matched, Config opened offline, and all 10 focused baseline tests passed. The original HDMI transition remains to be replayed.

In the stock progressive Smooth Motion path, a controlled case reused a single HDR source render across four output presentations. The compact peak worker instead throttles by wall time and has no distinct-source check at submit, and the processed source cache can miss after presentation context changes. The live run has yet to establish whether either condition caused repeated measurement on the reported scene. Keep the viewport unchanged as HDR peak analysis is introduced in a later controlled pass.

## Beta direct-output Smooth Motion trial

Following the Smooth Motion-only rollback, the engineering review approved an isolated A/B of the final mix pass. The public libplacebo change `1733c8601edec161b714e4a799c72a9f5e5aa2f0`, present in the bundled Beta fork, already blends in linear light and encodes the output transfer. It retains the overlay transfer contract. VP keeps its processed source cache, admitted geometry and output cadence. The opt-in Beta progressive path draws the existing mix directly to the acquired destination with the same output options. Stable, interlaced and advanced-subtitle composition retain their established output paths.

The source branch is `trial/vp0240-beta-direct-mix` under `E:\codex\videoprocessor\vp0240-beta-direct-mix-20261125`, based on verified official beta `d5f69645f4c818559d627d68eced177489c159d5` with bundled libplacebo `a89f8241bfd6c5f5affaa9c3ce39fa2b1767b8c7`. The isolated Release build `f2c27eea574f3e82fe15371a8c26388c831991d4` is under `artifacts\Smooth Motion AB f2c27eea574f`. The two folders ship the same executable, renderer, settings and diagnostics, with the same-source Config and output probe. The launch environment selects the path. Both start with Smooth Motion Auto, Scope, subtitles Off, and HDR peak detection Off. All 26 focused tests passed. Both packages had 125 matching payload hashes and their Config apps started offline. The launch switch was the only payload difference.

The D3D11 regression requires two actual mix contributors and checks the 10-bit output, border, translucent target overlay and fractional viewport across its tested output contracts. The exact HDMI replay, physical presentation gaps, and hardware timing remain open. Compare steady scope and the reported hard cut at 23.976 to 59.95 Hz, with cold and warmed caches measured separately. Keep HDR measurement attribution separate. The active installation was untouched.

## Peak On output-stall telemetry

The November 25 steady reconstruction review excluded fullscreen, resize, renderer, and profile transitions. The filtered sample saw zero skipped source IDs across 1,797 submissions and a 42.5 ms maximum submission gap. The earlier 88.5 ms cycle and 56.8 ms gap fell within the subtitle-profile settling periods. Only the Reconstruction launcher had a new Peak On log, so the matched Direct Mix comparison remains open.

The opt-in x64 Release trial at source commit 82722243a5ad618c539ee4acf49d24d5a7224857 is under artifacts\Smooth Motion Peak On Telemetry 82722243a5ad in the existing source worktree. The three launchers keep High Quality HDR peak detection on. Reference and Direct Mix start with subtitles Off. Reconstruction adds advanced subtitles on the library mix path. All three packages include the corresponding Config app, source patch, exact binary hashes and matched libplacebo. Config started offline in all three.

The trace correlates queue and render timings, source and output identities, transition epochs, cadence, capture and presentation timestamps, resolved GPU stage timings, and actual libplacebo peak dispatches. The shader time marked last available is asynchronous history. It can differ from the time of the specific output in the incident record. The focused GPU test verified contributor attribution in both manual and complete-composition paths and verified that reused packets dispatch no new peak analysis. A synthetic 42.5 ms gap retained preceding submissions and resolved the matching GPU sample. Matched live playback is pending.

## Reconstruction with the direct option

The existing progressive Reconstruction path already blends to the display with libplacebo. VP separately prepares a full raster with the caption composited and copies it into temporal history. The new opt-in path retains the completed surface directly in a bounded pool and keeps it alive through current-source recapture or a late cue revision. The subtitle shader, HDR mapping, cue eligibility, crop policy and renderer dependency are unchanged.

The x64 Release pair at source commit 386160eb0a022f76c30fbb50a944b6c91926fdde is under artifacts\Reconstruction Peak On AB 386160eb0a02 in the existing source worktree. Reference and Direct both start with Smooth Motion, Reconstruction, high-quality HDR peak detection and output-stall telemetry. The launch option is the only behavioral difference. The retained path reports complete_packet_library_mix_retained. Both Config apps opened offline and the 125 payload hashes matched in each folder. All 28 focused cases passed, including copied and retained output equivalence at the tested pixel and surface lifetime after history clear and late revision.

An isolated warm 3840x2160 RGBA16F history blit had a median GPU cost of 0.128 ms on the RTX 3080. The saving at live playback cadence and any change in the worst stalls still need a matched HDMI replay. Keep fullscreen and profile settling windows separate from steady playback.

## Off fit handoff trial

The November 25 Off playback log alternated between the native bottom edge at 1892 and the lower-bar fit ending at 2034. The native picture bounds stayed fixed. A controlled replay using the published beta policy at d5f69645f4c818559d627d68eced177489c159d5 reproduced the feedback loop with the existing two-source selection delay, without Direct Mix. Zero selection delay stayed stable.

Source d12c1c7da7ec93730d1772f547007d05d6727749 replaces the paused crop-recovery experiment with a guard at the Smooth Motion submission handoff. A buffered Scope fallback can advance submission order while a previously submitted, same-context Off fit is active. It does not clear that fit or renew its evidence. Once the hold expires, normal release applies. True native AR publications and full-raster authority follow the existing path. The replay holds the fit after the previously buffered Scope frames drain.

The isolated x64 Release build at artifacts\Off Framing Test d12c1c7da7ec starts fullscreen with Off, Scope, High Quality HDR peak detection, Smooth Motion, and enhanced logging. Only this test copy removes the renderer-ready Classic hotkey while retaining the existing shader shortcut. It includes source, hashes, Config, and the output probe. All 125 payload hashes matched and Config rendered offline. All 78 focused native and D3D11 tests passed. The exact live HDMI replay remains pending.

## November 26 Smooth Motion cue-boundary investigation

The current experimental source worktree is `E:\codex\videoprocessor\vp0240-cue-boundary-20261126`, branch `trial/vp0240-cue-boundary`, based on native-HDR-restored beta `24635341653800ce78520e51b77741abf4e072f0`. This is an investigation with synthetic fixture changes. No production rendering change or subtitle performance optimization has been implemented. The current design proposal is `C:\Users\bslac\Documents\ChatGPT\Madve\VP-0240-smooth-motion-proposal.md`. The original libplacebo HDR detector and its settings remain the reference.

A controlled SDR complete-packet fixture measured a midpoint value of 188/255 for an appearing subtitle and 188/188 for adjacent different subtitles, while a subtitle present in both packets remained 255. Fixture-known selected-only contributor output was also exercised as a proof of the mechanism. This is synthetic, fixture-known cue-boundary evidence, not authorization to infer real cue identity from geometry or timestamps. Current cue IDs can remain unchanged when lines are added. A real decision needs source-bound cue evidence and compatible picture and presentation context. HDR behavior, real cue changes and visual quality, motion continuity, and live HDMI remain unverified.

The isolated trial commit `a2ee2d4945df97d0ce8208cadb96f8358dfe5d9f` adds the characterization test only. Its full all-phase x64 Release build succeeded, and the focused GPU characterization passed 1/1. A separate scratch simulation of ideal source selection passed with fixture-provided perfect cue knowledge, in SDR with HDR peak detection off. The simulation does not establish a production implementation or prove live behavior. HDR, real subtitle identity and timing, and an optimization trial remain unverified. The story remains In Progress.

In a five-second Reconstruction window ending 2026-11-25 23:26:31, after Smooth Motion settled and before the next display transition, the older d12 trial logged 120 subtitle worker jobs and 120 detector calls, with 125 detector search passes. Completed worker-job wall time averaged 16.535 ms and peaked at 21.022 ms. The detector/search timings are nested within that total. This establishes a repeated worker-analysis cost and does not establish blocking or the cause of a presentation spike. That run used the older compact/authored HDR path, so it does not measure the restored .11 HDR detector. No steady-cue shortcut has been qualified.

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
