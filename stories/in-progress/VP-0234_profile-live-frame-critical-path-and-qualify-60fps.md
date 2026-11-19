# VP-0234: Profile the live frame critical path and qualify sustained 60 fps HDMI

## Status

In Progress

Requested 2026-11-18 after Eternals playback with the VP-0230 active detector trial. This story separates the performance investigation from ongoing AR accuracy and integration. Diagnostic-only implementation began 2026-11-18 in E:/codex/videoprocessor/vp0234-critical-path-20261118 at freshly fetched v2.0.6-beta acdb89bfb5e1796cda6359aa2f89632b78204a5b. Scope is opt-in CPU/wall/tail attribution; subtitle offload/reuse remains VP-0232/VP-0233 and detector behavior remains VP-0230. No performance improvement or 60 fps acceptance is claimed.

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
