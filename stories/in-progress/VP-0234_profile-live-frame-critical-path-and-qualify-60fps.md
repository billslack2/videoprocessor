# VP-0234: Profile the live frame critical path and qualify sustained 60 fps HDMI

## Status

In Progress

Requested 2026-11-18 after Eternals playback with the VP-0230 active detector trial. This story separates the performance investigation from ongoing AR accuracy and integration. Diagnostic-only implementation began 2026-11-18 in E:/codex/videoprocessor/vp0234-critical-path-20261118 at freshly fetched v2.0.6-beta acdb89bfb5e1796cda6359aa2f89632b78204a5b. Scope is opt-in CPU/wall/tail attribution; subtitle offload/reuse remains VP-0232/VP-0233 and detector behavior remains VP-0230. No performance improvement or 60 fps acceptance is claimed.

## 2026-11-19 diagnostic and build consistency update

Source branch `codex/vp0234-critical-path-20261118` starts at freshly fetched
`v2.0.6-beta` `acdb89bf`, explicitly retains tested `c1b130a2`, and adds opt-in
renderer CPU/wall/tail and source-submission diagnostics. The detector/subtitle
behavior is retained; no performance optimization or live 60 fps acceptance is
claimed. The first baseline's 24 aggregate windows cover 1,156 submissions:
foreground elapsed mean 14.650 ms, preparation 6.406 ms, render interval 7.356 ms,
separate swap 0.079 ms; foreground maximum 66.457 ms. These are partial window
aggregates, not per-frame p95/p99 or whole-pipeline CPU measurements.

At the user's request, `9e2df1f6` unifies every source-built project on v143 /
MSVC 14.44.35207 with matching MFC/ATL. Repository and personal AGENTS guidance,
a compiler-version guard, and a single-host build wrapper replace the former
v142/v143 split. `ab03d3c0` updates the direct AR test launcher too. Both are
committed and pushed. A full x64 Release migration rebuild and an ordinary
incremental build pass; 107 focused native/dependency tests and the direct AR
checks pass. Wrong-toolchain and obsolete-host checks reject conflicting builds.
See [build evidence](../assets/VP-0234/build-consistency-20261119.json).

The original LNK1103 corruption mechanism remains unproven; repeated rebuilds
are not the normal workflow. No beta merge, deployment, active-configuration
replacement or full GPU/live HDMI qualification occurred. Compiler migration is
an explicit additional difference from the earlier trial and must be controlled
in future matched timing. The isolated diagnostic playback package and real
59.94/60 fps capture/presentation qualification remain outstanding.

## 2026-11-19 first live diagnostic

Launched an isolated source `9e2df1f6` x64 Release package. Host/config/renderer
hashes and signed app-local Microsoft runtimes were verified. Current deployment
Scope/Classic cfg/state were copied into a new folder; only the isolated copy's
logging.debug changed to true, with its original retained. No deployment edits.

Thirteen steady visible five-second windows ending 15:43:05–15:44:05 contain
1,560 successful current-source submissions, zero repeated/skipped source IDs.
Input is 23.97633 fps. RenderLocked mean is 18.223 ms wall / 5.699 ms thread CPU;
preparation is 5.211 / 5.148 ms and render wrapper 12.928 / 0.461 ms. Total maximum
is 28.545 ms. Largest individual-window p95/p99 are 27.429/28.222 ms; they are not
whole-run percentiles. Submission gap mean/max 41.707/51.824 ms; callback-to-submit
mean/max 143.683/154.764 ms. Sampled oldest queue age 82.92–84.10 ms. Unlike the old
log, DXGI frame statistics are available and advance. External scanout is unmeasured.
[Aggregate evidence](../assets/VP-0234/live-steady-baseline-20261119.json).

Startup separately reached 586.032 ms, mostly CPU. Fullscreen exit at 15:44:10
recreated the renderer; later DXGI_STATUS_OCCLUDED is visibility evidence and is
not counted as a subtitle-induced stall. There is no demonstrated hundreds-of-ms
steady hold in this selected window. Current content was operator-approved as-is;
it is not a matched replay of the older incident and no improvement is claimed.

The demonstrated render interval has little scheduled CPU, pointing to waiting
or descheduling; a specific blocking call is not proven. Follow-up `c93d7f0e`
adds nested render-invocation, post-render-callback and HDR-status timings plus an
explicit budget-known denominator. A C++14 move compatibility issue was caught
and corrected before staging. Accounting/move checks and the x64 Release runtime
build pass; both renderer hashes and host identity verify. This second candidate
is staged separately, not yet live-tested. Do not add nested detail stages to the
parent render interval or interpret an unknown budget's zero-exceeded count as
headroom. Earlier capture/conversion/caller work remains outside RenderLocked.

Next: run the finer diagnostic while output is visible, attribute the render wait,
then exercise true 59.94/60 fps HDMI, caption/UI/late-worker cases and matched
before/after behavior. The current input remains 23.976 fps; 60 fps is unqualified.
No queues, timeouts, rendering quality, subtitle behavior or detector policy were
changed. No beta merge or deployment occurred.

## 2026-11-19 core 60 fps debugging continuation

The capture-side interruption was set aside at the user's request after recovery.
A healthy-input light-path cohort has ten five-second capture windows with 3,000
frames, 59.936–59.946 fps and no capture skips. Separately aligned analysis windows
contain 2,998 submissions: foreground mean 2.174 ms, preparation 1.844 ms, render
interval 0.199 ms, and separate swap mean 4.161 ms. Foreground maximum 6.053 ms and
swap maximum 11.991 ms are not same-frame measurements and must not be added.
Queue depth stays 8 in samples; oldest age is 115.59–118.77 ms. DXGI frame statistics
are unavailable in that cohort. Subtitle detector Analyze count is zero, so this
is not qualification of dense/changing subtitle work. The count difference between
capture/analysis windows is not inferred capture loss.
[Light-path evidence](../assets/VP-0234/60fps-light-path-baseline-20261119.json).

With user approval, restarted only the isolated c93d7f0e player using the opt-in
probe environment; saved settings were preserved. Schema2 nested timings now
appear. The initial visible window averages 0.153 ms render invocation, 0.088 ms
post-render callbacks, negligible HDR-status collection, and 12.887 ms swap wait.
Total RenderLocked mean/max is 15.158/32.591 ms. Startup and two skipped source IDs
remain explicit; this is not a steady acceptance window. Only eight samples had a
known source budget. Subsequent window occlusion returns no successful presents,
so those periods are excluded from visible playback qualification.

This identifies presentation waiting on this light 60fps input; it does not
attribute the earlier 24fps/Dolby-Vision render-wrapper delay or establish a
subtitle optimization. The probe remains running. Next required live sample is
visible/uncovered playback with changing captions and volume/UI overlays, followed
by matched path/caption/reset and late-worker coverage. No code, quality, queue or
configuration changes were made to improve the reported timing. Full60fps and
subtitle/pixel-preservation acceptance remain open.

## 2026-11-19 subtitle cold/warm preparation and profile control

Current c93d7f0e logged complete background subtitle preparations of 77,239.64 ms
and later 98,253.43 ms in separate cohorts. These are whole-job elapsed times, not
pure compiler CPU. Existing VP-0225 shader-startup evidence already identifies the
all-variant gate and heavy graph complexity; this investigation reuses that work.
New diagnostic folders omitted previous subtitle cache data, contributing cold
starts. This is not attributed to24fps-on60Hz cadence or assumed GPU contention.

The user approved a same-folder graceful restart. Saved cache grew from685,124 to
1,204,308 bytes; the relaunched player loaded44 objects. Configuration hash stayed
unchanged. Startup selected Classic, so the live shortcut was used to select and
verify Reconstruction. Warm background preparation completed in307.80 ms.

That warm activation still FAILED the real-time requirement: source4463 spent
241.664 ms inside the render invocation (242.106 ms wrapper), yielding a259.015 ms
submission gap and11 skipped source IDs after backlog recovery. Live libplacebo
logs report173.107 ms GLSL-to-SPIR-V,26.675 ms SPIR-V-to-HLSL and16.181 ms
HLSL-to-DXBC translation. Thus foreground compiler work is demonstrated; a precise
missing prewarm graph is not yet identified. The stalled layout was a full-output
raw presentation with no overlays and unavailable configured screen geometry;
this correlation does not authorize weakening pixel-preserving fallback or AR.

The user stopped automated Computer Use with Escape; no more UI input followed.
The user then manually cycled all profiles and reported that they load fine.
In the observed16:53:10–16:54:45 control window,5,988 submitted source IDs contain
zero repeats/skips and a21.559 ms maximum gap. Another background preparation
finished in141.34 ms, with no slow compiler translation events in that window.
This supports successful warm switching; the remaining targets are cold-start
availability and the first-use foreground cache miss, not a recurring warm-mode
stall. Actual subtitle/cleanup pixel and dense-cue qualification remain separate.
[Warm-cache and manual-cycle evidence](../assets/VP-0234/subtitle-warm-cache-20261119.json).

Keep the user's24fps HDMI input on the60Hz test monitor separate from the real
display's24Hz operation and from genuine59.94fps input. Existing cache reuse
improves availability without code/quality changes; neither the259 ms activation
nor full60fps/subtitle acceptance is declared fixed. Current selected subtitle
profile after the user's cycle is Classic. Playback was left under user control.

## User story

As a viewer, I want to know what consumes the live frame budget and obtain measured improvements that sustain incoming HDMI video without disrupting picture, subtitles or aspect detection.

## Evidence and starting context

The tested isolated candidate is `E:/codex/videoprocessor/ar-active-test-c1b130a2`, with active/control launchers, build receipt, validation records and `logs/vp.log`. Source is `c1b130a2b1ff301038788735892351a5b8a4c040` in `E:/codex/videoprocessor/ar-confidence-shadow-20261117`, branch `codex/ar-confidence-active-20261117`. These identify the observed experiment, not the current integration baseline. Preserve other agents' work and discover the current remote beta before source changes.

The user reported apparently correct AR and subtitles but little visible improvement. That is useful playback evidence, not a matched accuracy or performance comparison. VP-0230 owns matched-frame boundary accuracy, missed bars, false crops, transition responsiveness and integration.

The overlay's New CPU/wait means a new source frame, not the new AR detector. It measures foreground elapsed work including blocking inside calls; subtitle Prep is a subset with a different sample denominator. Present/swap has separate timing. GPU time overlaps CPU/wall time and must not be added to it.

A representative 2026-11-18 10:31:27 diagnostic window reported approximately 18.0 ms foreground work: 11.0 ms in the render interval, 6.6 ms in live preparation and 0.4 ms elsewhere. A nearby upload window averaged 3.5 ms, inside preparation; its window differs. The render interval includes wrapper/diagnostic work and is not an isolated GPU execution measurement. Background AR observations were about 1 ms; a separate subtitle worker window averaged about 16.8 ms elapsed per completed job. These are initial attribution clues, not pure CPU measurements or evidence that every job is required for every frame. Earlier render-associated submission gaps reached hundreds of milliseconds.

Input was 23.976 fps (41.71 ms per frame), with display near 59.95 Hz. This does not qualify 60 fresh input frames per second. At 60 fps the source interval is 16.67 ms; repeating current per-frame work around 18–19 ms on the mandatory path would exceed it. Recheck current logs and exact configuration before treating these observations as a baseline.

## Required work and first deliverable

1. Pin the running package, source, renderer dependency, input format/rate, display rate and relevant settings. Read active configuration rather than asking the user to restate it. Compare the observed local candidate with current remote beta explicitly; do not silently substitute one for the other. Use actual source pins for useful older RC6/pre-RC6 references.
2. Trace and, where needed, minimally instrument capture-to-submission/presentation. Separate thread CPU from elapsed blocking, upload/conversion/copy, foreground preparation, render invocation, synchronization, diagnostics/callbacks and Present. Identify nested versus exclusive intervals and asynchronous overlap. Attribute subtitle worker service time, queue wait and verified reuse cadence separately. Avoid extra synchronous scans or excessive per-frame logging.
3. Produce a short, actionable baseline and the smallest source-pinned x64 Release diagnostic candidate needed for real playback. Prioritize an early live test over an exhaustive redesign or hours of unrelated tests. State precisely what each new measurement proves and which uncertainty it resolves.
4. Optimize the largest demonstrated mandatory cost first, using matched before/after inputs and settings. Coordinate subtitle changes with VP-0232/VP-0233 and general stall recovery with VP-0225 instead of implementing competing fixes. Preserve rendering quality and user settings; do not silently disable features to meet the budget.
5. Qualify true 59.94/60 fps HDMI separately from 24 fps content on a 60 Hz display. Measure the complete critical path, tail costs, actual source/submission/presentation gaps and bounded end-to-end latency. Report unavailable scanout evidence explicitly. Separate cold compilation and genuine device transitions from steady playback, while recording their user-visible effects.

## Acceptance and safeguards

- Report matched baseline/candidate mean, p95, p99 and maximum critical-path costs, worker service/queue times, source progression, presentation evidence, queue age and backlog trend. Establish headroom within the applicable frame budget; average latency and zero drop counters alone do not demonstrate success.
- Video continues on schedule when optional AR/subtitle results are late, missing, stale or failed. Never hold/replay a whole previous frame waiting for them, increase queues/timeouts to hide stalls, or reuse previous movie/background/cleanup pixels as current proof.
- Preserve picture and captions with exact source/context identity. Keep presentation fallback separate from measured active-picture authority. Detection remains independent of screen size, CIH, fitting and standard-ratio preferences.
- Exercise dense/changing subtitles, unchanged multi-second cues over moving backgrounds, volume/UI overlays, slow or missing analysis, profile/format resets, Off framing and genuine AR transitions. Label seek/rewind discontinuities separately from ordinary playback failures.
- Run focused tests appropriate to changed components, including both shipped renderer dependencies when affected. Keep an honest record of incomplete suites and unavailable live qualification. Obtain real-world playback results before claiming improvement.
- Build only x64 Release for playback. Use an isolated candidate first; any deployment preserves existing configuration and uses matching host/renderer artifacts with verified source identity and hashes. Do not commit private footage or raw user configuration.

## Related work and ownership

- [VP-0230](../in-progress/VP-0230_adaptive-matte-geometry-and-history.md): AR boundary accuracy, confidence experiment and integration remain in the existing AR chat.
- [VP-0225](../in-progress/VP-0225_subtitle-processing-performance-and-repeatable-playback-stalls.md): broader playback-stall investigation and prior critical-path evidence. This story is the bounded current-build attribution and 60 fps qualification follow-up; reuse its instrumentation and evidence before adding more.
- [VP-0232](VP-0232_move-subtitle-verification-off-renderer-thread.md): bounded off-thread subtitle verification.
- [VP-0233](VP-0233_recover-and-improve-subtitle-cue-reuse-performance.md): cue reuse performance.

## Tracker audit at creation

Fetched and fast-forwarded authoritative main on 2026-11-18. Before allocation there were 251 canonical story files and 251 indexed IDs, with no duplicate IDs or missing file/index counterparts. Highest root was VP-0233; VP-0234 increases the registry to 252 items. Existing unrelated uncommitted edits are preserved.
