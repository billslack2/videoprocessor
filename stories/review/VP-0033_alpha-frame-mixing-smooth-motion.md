# VP-0033: Alpha renderer libplacebo frame-mixing smooth motion

## Status

Review (2026-10-20). [PR #172](https://github.com/billslack2/videoprocessor/pull/172) merged into current beta at `76d922ea75aa191fcdb934dc4aa08a24b4ef21ef` after current-beta integration build and 905 native plus five Config checks passed. At the user's request, a separate test deployment is ready at `C:\Videoprocessor\vp-test-smooth-motion-cache-89382446`, using the completed x64 Release build from `893824466396a0c88c4e096d22b5dc513aab3a49`, which also preserves the installed local fixes from `c308b9f514336e5a796b9aae286c5d5a8808756e`. The user's configuration was copied byte-for-byte. The active installation and its processes remain unchanged. No additional test run or launch was performed for this separate copy. Live visual/performance acceptance remains pending.

## User story

As an Alpha renderer user whose display cannot run at the captured content
cadence, I want an optional libplacebo frame-mixing mode that reduces cadence
judder while keeping VP stable and predictable.

## Terminology and non-goal

Libplacebo calls this frame mixing/interpolation. It blends or mixes timed
neighboring frames through `pl_queue` and `pl_render_image_mix` to reduce
judder from source/display-rate mismatch.

It is **not** optical-flow, motion-vector, or motion-compensated intermediate
frame generation. Do not market it as true motion interpolation and do not
attempt to emulate motion-compensated processors in this story.

## Scope

- Alpha/libplacebo renderer only.
- Optional and disabled by default.
- Use only when a verified actual display cadence cannot be matched through the
  existing refresh-rate selection policy, or when a user explicitly selects it.
- Define bounded latency and queue behavior suitable for VP's live capture
  path.

Do not replace Alpha's normal direct `pl_render_image` path for matched
cadence, nor weaken the existing display-rate/restart safety rules.

## Required implementation work

1. Decide whether VP's existing queue can safely feed libplacebo's `pl_queue`
   or whether a small renderer-owned timing adapter is required. Preserve
   source-buffer lifetime and the Alpha low-latency queue guarantees.
2. Feed monotonic PTS, accurate durations, and actual predicted-vsync timing.
   Explicitly handle discontinuities, renderer reset, seek-like source changes,
   display-mode changes, and missing future frames.
3. Select and document an initial mixer policy. Make any blend radius, latency,
   and mismatch thresholds explicit and bounded.
4. Require a verified mismatch before AUTO activates. A stale display target or
   a transient timing sample must not activate blending unnecessarily.
5. Provide clear OSD/log state: disabled, unavailable, matched-rate bypass,
   active mixer, source/display rates, blend/latency policy, queue health, and
   fallback reason.
6. Add configuration and HTML help only after the behavior is stable. The
   setting must make the non-motion-compensated limitation clear.

## Verification

- Test 23.976 at 59.94/60, 25 at 60, 29.97 at 60, and matched-rate controls.
- Confirm matched cadence bypasses mixing and retains the current low-latency
  behavior.
- Measure visual judder reduction, blend/ghosting artifacts, end-to-end
  latency, queue depth, dropped/repeated frames, and audio synchronization.
- Test renderer switching, refresh switches, HDR/SDR/LLDV transitions,
  display-mode changes, and source discontinuities. No reset or switching loop
  may result.

## Acceptance criteria

- VP exposes an optional, accurately named frame-mixing feature for verified
  mismatched cadence.
- It is disabled by default and never replaces normal refresh-rate matching.
- Timing, buffering, and fallback behavior are bounded and observable.
- The feature does not claim or attempt true motion-compensated interpolation.

## Implementation progress — 2026-10-14

User authorized full Motion/chroma configuration and merges into current beta and DV2. Validation is narrowed to VP configuration loading, persistence and libplacebo integration; visual-quality comparisons are not required for this request. Beta PR [#158](https://github.com/billslack2/videoprocessor/pull/158) merged; DV2 is preserving its independent additions and newly merged subtitle/aspect-ratio fix before retesting. No deployment or active configuration changes.

## Merged implementation — 2026-10-14

- Beta: [PR #158](https://github.com/billslack2/videoprocessor/pull/158), merge `f06430fd1d081cc0d5fdbddec1b45d9671eac34f`; tested source `304422d1f8616e59a7780a2ab5956c19fc57ef82`.
- DV2: [PR #159](https://github.com/billslack2/videoprocessor/pull/159), merge `4c080e8d5f2d2d472c85ed1393aeeae5ff98db56`; tested source `ead3f04552e1d0b56eb6c2170752f533e9c67224` preserves DV2 base `4dac8b8658033f8c123614a232ab07fce4323b0f`, including its new subtitle/aspect-ratio fix.
- Both merged trees are byte-for-byte identical at the source-tree level to their tested feature commits. Separate clean x64 Release builds passed, including stable and beta libplacebo plugins.
- Beta: 259 native integration checks and three focused Qt checks pass. DV2: 324 native checks, including tunnel/luminance/analysis diagnostics and renderer reset policies, plus four focused Qt checks including existing DV/LLDV controls, pass.
- Motion page exposes Smooth motion Off/Auto/Always (default Off), all four bundled mixers, Deinterlacing Auto/Off/Force, Weave/Bob/YADIF/BWDIF (default BWDIF), reported/overridden field order, field/frame cadence and YADIF spatial-check control. Scaling exposes explicit independent chroma reconstruction, default Bicubic without Auto. Compact tabs retain one row with scroll overflow.
- Capture field order, owned bounded GPU history, timestamps, field submission, discontinuity/reset behavior and configuration persistence are validated. Historical visual-quality comparisons are not required for this request: the user explicitly narrowed testing to VP configuration and libplacebo integration.
- Source subtitle cut/paste composition currently bypasses temporal processing with a diagnostic. Native HDR passthrough remains deferred. No playback/deployment or active configuration edits were performed.
- [Build/test receipts and Motion screenshot](../assets/VP-0032-0033/merge-20261014/README.md). Host/plugin API is 26 on beta and 28 on DV2; matched binaries are required within each branch.

## DV2 deployed — 2026-10-14

User authorized deploying DV2. Deployed exact merged `dv2` tip `4c080e8d5f2d2d472c85ed1393aeeae5ff98db56` from a successful clean x64 Release solution build. Host, stable/beta renderer plugins, Config UI, Config discovery and output probe plus matching runtime records and HTML help were backed up and updated; all copied hashes match build artifacts. One differing Qt offscreen plugin was also backed up and updated; remaining dependency hashes already matched.

Backup: `C:\Videoprocessor\vp\deployment-backups\dv2-4c080e8d5f2d-20261014-133826`. Active `VideoProcessor.cfg` and `VideoProcessor.state` remained byte-identical; no configuration edits. 199 focused config/parameter/motion/plugin checks passed. The deployed Config app opened Motion against a disposable copy of the operator config, exited successfully, and showed default Off smooth motion and Auto/BWDIF deinterlacing. VP playback was not launched. [Deployment receipt, build/test logs and screenshot](../assets/VP-0032-0033/deploy-dv2-20261014/README.md). Review status is unchanged.

## Motion UI explanation follow-up — 2026-10-14

User reported blank tooltips and unclear relationships between Motion controls. [Beta PR #160](https://github.com/billslack2/videoprocessor/pull/160) merged at `c71b44593cad5ba5dd8bd7ec369f0d3a5965ff72`; [DV2 PR #161](https://github.com/billslack2/videoprocessor/pull/161) merged at `2a2b4242dfc950381fbdc15c4125ca4ead910f82`, preserving DV2 additions.

Smooth motion and Deinterlacing now have compact named sections and inline explanations. Mixing filter is grouped with smooth motion; Algorithm and Field order with deinterlacing. Advanced deinterlacing holds cadence and YADIF-specific tuning. Each Motion control and label has meaningful hover help covering scope, defaults and choices. The profile loader no longer emits newline-only blank tooltips. Playback and persisted option values are unchanged.

Focused x64 Release Qt checks passed on both branches for grouping, all seven Motion tooltips, explicit mixer relationship, persistence and every-page round trips; the DV/LLDV mode preservation check also passed on DV2. [Actual 1040x700 layout](../assets/VP-0032-0033/motion-help-20261014/motion.png). Replacement deployment is pending Release build completion and closure of the user's currently running VP/Config applications; no process was interrupted.

The exact merged DV2 commit `2a2b4242dfc950381fbdc15c4125ca4ead910f82` subsequently completed its clean x64 Release solution build. Production Config opened successfully against a disposable copy of the active config and displayed the corrected grouping. [Build log](../assets/VP-0032-0033/motion-help-20261014/dv2-release-build.txt) and [native preview](../assets/VP-0032-0033/motion-help-20261014/native-motion.png) are saved. Replacement deployment remains pending closure of VP and Config, requested from the user; the prior `4c080e8d5f2d` deployment remains installed.

User authorized and completed replacement deployment of DV2 `2a2b4242dfc950381fbdc15c4125ca4ead910f82` at 2026-10-14 14:01 EDT. Host, both renderer plugins, Config and matching runtime records were backed up at `C:\Videoprocessor\vp\deployment-backups\dv2-2a2b4242dfc9-20261014-140116` and replaced with hash-verified x64 Release outputs. Configuration and saved state remained byte-identical. Deployed Config opened the corrected Motion page against a disposable config copy and exited successfully. Playback was not launched. [Deployment receipt](../assets/VP-0032-0033/motion-help-20261014/deployment.json) and [deployed screenshot](../assets/VP-0032-0033/motion-help-20261014/deployed-motion.png). This supersedes the pending deployment note above; Review status remains unchanged.

## 2026-10-16 mixer correction validation

- The real stable/beta pl_frame_mixers tables contain mitchell_clamp, not cubic. UI and documentation now expose the valid name; legacy files map safely without being rewritten on open.
- Runtime refuses to report mixing active when its selected filter is unavailable. Invalid motion strings preserve inherited valid settings.
- Tests require every offered mixer to resolve to a non-null native filter in both bundled runtimes, exercise GPU rendering, and cover chroma mappings and comment-preserving config round trips.
- All 40 data exports used by the render-parameter mapper exist in both bundled libraries.
- Evidence: E:\codex\videoprocessor\nls-live-preparation-20261016\nls-test-results\mixer-validation.json and mixer-validation.trx; runtime audit in renderer-choice-export-audit.json.

## Smooth-motion measurement caching and OSD follow-up - 2026-10-20

The user observed an approximately 13.33 ms allocated frame time with 23.976 fps input and 75 Hz smooth-motion output. This is the scheduled output interval, whereas fresh source frames arrive approximately every 41.71 ms. The OSD should distinguish both intervals while keeping output-render utilization tied to the output interval. Replacing the output denominator with the source interval would conceal genuine output-rate work.

User authorization: "ok -- lets try the caching and also updating OSD". Scope is a conservative follow-up to this story: cache repeated source pixel measurements only when the source identity, measurement settings, and analysis geometry match; retain per-output crop decisions, timers, presentation state, blending, tone mapping, and subtitle ordering. Do not infer permission to deploy, overwrite configuration, or merge/release.

### Readiness review

- The implementation starts in a clean source worktree from the newly discovered and fetched beta tip, rather than a stale local checkout.
- The design preserves renderer pipeline placement and source-buffer lifetime; only reusable measurement results may be retained.
- Invalidating frame identity, analysis configuration, geometry, source/reset epochs, or unavailable/unsafe sample storage must not yield a stale cache hit.
- State-dependent policy and time progression must continue once per output; the cache must not freeze delayed crop decisions or presentation transitions.
- OSD changes distinguish the captured source interval from the scheduled output interval and retain the output-render percentage denominator.
- Focused deterministic tests must prove repeat reuse and meaningful invalidation, including state/timing progression; an x64 Release build and existing relevant crop/temporal/OSD regression checks qualify review readiness.
- No new setting or migration is planned. Live visual/performance acceptance, and any deployment, remain separate review decisions.

### Progress

- 2026-10-20: Current beta and source identity verified; caching and OSD implementation started on the branch/worktree recorded in Status. Story returned from Review to In Progress for this authorized follow-up. Historical mixer, Motion UI, and deployment evidence above remains intact.

The previous Review status (2026-10-16) recorded local mixer validation in `E:\codex\videoprocessor\nls-live-preparation-20261016` on `v1.4.02-beta` at `51b211dc`: clean x64 Release solution and beta renderer builds, 113 targeted native tests, and three config-editor tests passed. That change was not deployed at that point and live visual review remained. This snapshot is retained as historical evidence and does not identify the current follow-up source tip.
### Review evidence - 2026-10-20

- Source commit: `bac39f4032f41b1a62f83c51e80b9d31501daa1b`, pushed to `codex/smooth-motion-measurement-cache-20261020`; draft [PR #172](https://github.com/billslack2/videoprocessor/pull/172) targets `v1.4.02-beta` from `72bcc9ad1fc29142fb91928461e567f5725dcd0b`.
- Repeated source pixel observations are cached only within one source output batch. Reuse requires matching source identity, layout, owner/control block, observation domain, measurement policy, continuity, and exact retention crop. A single output and borrowed buffers use the direct path. Only read-only observations are reused; crop decisions, evidence/timeline progression, subtitle composition, blending, tone mapping and output presentation retain their existing order and cadence.
- OSD now reports the source frame interval and scheduled output interval separately, for example approximately 41.71 ms at 23.976 fps and 13.33 ms at 75 Hz. Output-render utilization remains tied to the output interval.
- Full x64 Release solution build passed with zero errors (37 compiler warnings), including both stable and beta renderer variants. Build log: `E:\codex\videoprocessor\smooth-motion-measurement-cache-20261020\build-solution-release.log`.
- All 417 selected native regressions passed, including ten new cache tests; coverage includes timing separation, crop evidence/transitions/timeline progression, GPU motion/subtitle paths, upload reuse, and OSD placement. TRX: `E:\codex\videoprocessor\smooth-motion-measurement-cache-20261020\TestResults\smooth-motion-cache-regressions.trx`.
- Independent source and OSD review found no concrete issues. Build/source identity and SHA-256 hashes for the GUI and both renderer variants are recorded in `E:\codex\videoprocessor\smooth-motion-measurement-cache-20261020\x64\Release\smooth-motion-cache-validation.json`.
- Remaining decisions and validation: review/merge of draft PR #172, any separately authorized deployment, live HDMI/visual validation, and a measured performance trial. No performance gain has been claimed from the deterministic regressions. No deployment or active configuration change was performed. This follow-up is ready for review, not accepted Done; earlier visual acceptance remains pending.
### Merged beta and separate test deployment - 2026-10-20

- Merge authorization: "merge this"; subsequent deployment authorization was narrowed to a separate test folder with the current configuration and no further rebasing or tests. PR #172 merged at `76d922ea75aa191fcdb934dc4aa08a24b4ef21ef`, with the exact tested feature/current-beta integration `3140d2e8e396650b0ec66263550ab70ee5c1072b`.
- The user explicitly chose to preserve installed subtitle/display/AR fixes in a separate deployment build. Build source `893824466396a0c88c4e096d22b5dc513aab3a49` combines merged beta with installed `c308b9f514336e5a796b9aae286c5d5a8808756e`. Full x64 Release solution build completed with zero errors; log: `E:\codex\videoprocessor\deploy-smooth-motion-cache-preserved-20261020\build-preserved-deployment-release.log`.
- Separate test folder: `C:\Videoprocessor\vp-test-smooth-motion-cache-89382446`. Host, both renderer plugins, Config, discovery bridge and output probe match the completed Release artifacts by SHA-256; existing app-local dependencies, shaders and LUTs were copied. Active `VideoProcessor.cfg` and state were copied; configuration bytes match the source exactly, with no edits. `Start Test.cmd` sets the isolated working directory. Receipt: `test-build-receipt.json` in that folder.
- The original `C:\Videoprocessor\vp` installation, configuration and running processes were not changed, stopped or relaunched. The separate copy was not launched. User will perform the manual trial.
- Earlier complete-suite attempts on the preserved branch found stale subtitle/default/display test expectations and an offscreen/native-message failure; these were investigated, and test-only source adjustments remain local and unbuilt. They are not claimed as clean full-suite acceptance. The user requested no further testing before creating the separate copy; no additional tests were run for this deployment.
- User clarified AR acceptance: clean AR changes must respond promptly; gradual changes must hold the current framing and snap once the new AR settles. No new transition-policy changes were introduced by the measurement cache. This distinction remains part of live visual acceptance.

### Subtitle-change render deadline follow-up - 2026-10-20

- User observed continuing drops at subtitle changes and required lower processing cost even at ordinary 60 Hz. Live logs show queue-capacity drops with zero capture misses, plus 21-24 ms render-thread peaks. Cold shader compilation explained the initial large burst but not all later drops.
- A previously uncached Classic dense bar-content scan sampled every luma position using the full source validator and luma/chroma conversion. Source `177b7c8cc1866356726de768999bfd8eacc9f978` on `codex/subtitle-render-budget-20261020` reads planar luma by row with one validation and caches only the raw result within the same source output batch, keyed by exact source/context/bounds/black floor. The branch starts from fetched current beta `76d922ea` and preserves the earlier user-authorized installed fixes through `89382446`. No subtitle detection frames are skipped and no AR confirmation, hold/drift, or output composition policies change.
- Exact-output benchmark linked the actual v142 x64 Release library. Captured continuous-subtitle DV: 5.239 ms original versus 0.328 ms row scan (15.96x); synthetic UHD planar variants 15.4-16.0x. Packed/RGB first-scan costs remain approximately unchanged; same-frame reuse applies there too. Checks compare full extent/peak counts, per-row thresholds/spans and context invalidation. [Benchmark output](../assets/VP-0032-0033/subtitle-bar-scan-20261020/benchmark.txt). This is a scan-level result, not a measured full-render guarantee.
- Clean x64 Release host and stable/beta renderer build succeeded with zero errors or warnings: `E:\codex\videoprocessor\subtitle-render-budget-20261020\build-subtitle-budget-clean.log`. No full native/Qt suite was run, following the user's request to avoid further suite testing for the isolated trial.
- New isolated test folder: `C:\Videoprocessor\vp-test-subtitle-fast-177b7c8c`. The current running test configuration was copied byte-identically, including smooth_motion:auto; existing saved shader caches, state, runtime dependencies, shaders and LUTs were preserved. Host and both plugins match the new Release build by SHA-256. Config/probe tools retain the prior preserved build because they were unchanged. [Deployment receipt](../assets/VP-0032-0033/subtitle-bar-scan-20261020/deployment.json).
- Both previous deployments and running processes were left untouched; new copy was not launched. Live subtitle transitions, end-to-end deadline/dropped-frame results, native 60 Hz input, and fast clean AR changes versus gradual wait-and-snap acceptance remain pending. Story stays Review; this optimization is pushed but not merged into beta.

### Source-rate picture processing and alternating analysis - 2026-10-20

- User authorized moving expensive processing before smooth-motion blending, clarified that expensive analysis should run on every input frame through 30 fps and exactly every other input frame above 30 fps, then explicitly requested rebase, standard deployment, stopping VP/Config as needed, and starting VP.
- Source `1e0edf78e73844ad7f2d3ef26c1f27911d6fccb4`, pushed on `codex/source-rate-picture-processing-20261020`, is rebased onto current beta `0a6cdc2dbbb68a14c3b687d8e2dbcdad4878cd2b` (PR #176). Rebase conflicts preserve beta's new abrupt-boundary/reset fixes and the prior user-authorized local display/captioned-transition additions. This deployment branch is not merged into beta.
- Each retained input picture/deinterlaced field is composited with its own subtitle, scaled, tone/gamut mapped and calibrated into a keyed output-sized float cache before blending. Output frames blend complete processed pictures in linear light, then encode/dither and add the live OSD. Source processing uses a separate renderer so output passes cannot reset source HDR peak history. Source/field/neighbors, geometry, target color/representation, settings epoch, calibration and hooks control reuse. OSD labels the aggregate GPU utilization as GPU output load.
- Full subtitle measurement follows the input sequence: one scan per input at <=30 fps, odd input sequences only above 30 fps (60->30, 120->60). Repeated outputs read completed work without submitting new scans. Explicitly skipped frames remain unknown and add no confirmation; lookahead compares actual independently measured stride-two observations. Current-pixel continuation and cheap/native AR checks retain input cadence. No video frames are deliberately discarded.
- Final clean x64 Release product build passed with zero errors (8 warnings): `E:\codex\videoprocessor\source-rate-picture-processing-20261020\build-source-rate-release.log`. All 645 selected native checks passed, including real GPU motion/HDR/chroma/crop/overlay tests, cache invalidation, source-rate counters, alternate analysis and current beta AR transitions. [TRX](../assets/VP-0032-0033/source-rate-20261020/targeted-regressions.trx). Initial checks exposed raw fallback after failed native preparation and stable-runtime gamma-space blending; both were fixed. The test module required a rebuild without test-only LTCG to work around v142 debug-information linker corruption; production binaries retain their normal Release settings. No complete-suite acceptance is claimed.
- Standard deployment completed at `C:\Videoprocessor\vp`, backup `C:\Videoprocessor\vp\deployment-backups\source-rate-1e0edf78-20261020-232828`. Host, stable/beta plugins, Config, discovery bridge and output probe match the completed Release artifacts by SHA-256. Configuration and saved state were backed up; no configuration edits were made. Current configuration SHA-256 is recorded in the [deployment receipt](../assets/VP-0032-0033/source-rate-20261020/deployment.json), with saved smooth motion Off.
- Only exact standard VP/Config process paths were stopped. An initial broad process-filter command was rejected by automatic review before execution; the retry narrowed the process paths and left alternate installations alone. Standard VP started successfully as PID 67884. Live logs confirm clean host/renderer identity `1e0edf78` and 23.976 input with analysis stride 1; all six deployed hashes were reverified after startup. Live smooth-motion performance/visual acceptance remains pending because the preserved standard configuration has smooth motion Off. Story remains Review.
