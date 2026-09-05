# Render-load OSD stats — true GPU frame time

Adds madVR-style render figures to the Ctrl+I OSD and to `vp.log`, so a VP
Renderer quality setting can be measured against the frame budget instead of
inferred from dropped frames.

**Status.** Verified on the rig on 2026-09-05 against real Dolby Vision content:
RTX 3060 Ti at 4K 23.976p, GPU average 4.354 ms, **session peak 10.053 ms
(24.1%)**, CPU 4-5%, zero drops over 4 min 10 s. The session peak was still
climbing when the clip ended, and the 10-second window under-read it by 2x at
one point - both the behaviour the design predicted.

> Branch `anevard/render-load-osd-stats` on `github.com/anevard/videoprocessor`.
> **How to read these numbers — including two ways they are easy to misread — is
> `RENDER_LOAD_INTERPRETATION.md`, beside this file.** Read it before quoting any
> figure from the OSD.

---

## The gap it closes

Nothing in the build measured true GPU execution time, and nothing measured CPU
at all. `render_ms` in the log is wall time around `pl_render_image`, which on a
vsync-paced loop is mostly the BLOCKED wait for the display - so raising
`upscaler` to an EWA filter, `peak_detection` to `HighQuality`, or adding a 65³
LUT can cost 10 ms of GPU time and barely move any number now recorded. The
first symptom is a dropped frame, with no figure explaining it.

Nor was there any record of the worst frame. The only rolling window in the
build (`RollingPerformanceWindow`, in the frame formatters) keeps a recent
average and max but no all-session peak, so a stall that dropped frames left no
trace once it aged out.

## What it adds

Three tiers of the same measurement, answering three different questions:

- **average over 10 seconds** - what it normally costs
- **peak over 10 seconds** - what it just did
- **session peak** - whether it EVER hit the ceiling

The window is measured in **seconds, not frames**. A frame count silently means
a different span at every refresh rate, so two runs cannot be compared.

Three costs are measured, and they are not interchangeable:

- **gpu** - GPU execution time of the render passes, from libplacebo's per-pass
  timer queries (`pl_render_params.info_callback`). The D3D11 backend implements
  these with real `TIMESTAMP` / `TIMESTAMP_DISJOINT` queries, read back
  asynchronously, so nothing blocks the render thread. **This is the number that
  scales with the quality settings, and the only one on the OSD.**
- **render** / **swap** - CPU wall time around the render call and the present.
  Log only. On a VP-owned `FLIP_DISCARD` swapchain these measure **waiting, not
  work**: `render_ms` reads ~30 ms of a 41.71 ms frame while the GPU uses 4.4 ms
  and nothing drops, because the present queues and the back-pressure lands
  inside `pl_render_image`. Which of the two holds the wait is a property of the
  swapchain path, not of load, so neither is shown on screen.

A **warm-up guard** discards samples until the GPU timers resolve and 3 seconds
have passed, re-arming after a renderer restart and after a backlog recovery.
Without it the session peak would always be shader compilation - one measured
483 ms.

The **session peak deliberately survives `Reset()`**. The rolling window is
cleared by `ResetTimingAfterBacklogRecovery`, which is triggered BY a render
stall, so today a stall bad enough to drop frames erases its own evidence.

New OSD rows under the Queue row, as measured at 4K 23.976p on an RTX 3060 Ti:

```
GPU Render:      4.42 ms avg, 6.71 peak in last 10s  (16%)
 - Budget:       41.71 ms/frame  (23.976 Hz)
 - Session peak: 7.94 ms  (19%)  over 143820 frames
 - CPU:          12% now,  34% session peak
```

The percentage is **peak against one refresh**, not average. At 100% a single
frame needed a whole refresh to draw, which is a drop or a repeat on screen.

`CPU` is the first CPU measurement in the build - the project had no call to
`GetProcessTimes` or any other CPU-time API. It is CPU actually charged to the
process, process-wide because VP's real CPU cost is capture and the V210->P010
conversion rather than the render call. It writes **no periodic log line**: one
line only, when a new session peak reaches at least 4x this machine's own
running baseline and 10%. A fixed threshold was tried first and measured
useless - VP idles at 4-5%, so anything alarming would never have fired.

The GPU figures are appended to the `Alpha presentation telemetry:` log line at
the **end**, so any existing parser of that line keeps working. `render_ms` and
`swap_ms`, which predate this change and feed the post-stall stall detector, are
untouched.

How to read all of it: `RENDER_LOAD_INTERPRETATION.md`.

## Borrowed from VP-0161

`RendererHealthTracker::SanitizedDuration` is adopted here, because a session
peak has no way to forget a bad sample: one non-finite or negative duration
would pin it for the life of the renderer. A GPU pass time over a second is
rejected on the same grounds - that is a failed timer readback, not a cost.

VP-0161 also has stall counting and a Warming/Good/Degraded state that this
change deliberately does NOT duplicate. It overlaps `PostStallResetAdvisor` and
is the heart of that branch; reconciling the two OSD panels belongs in a
conversation about VP-0161 rather than here.

## The budget can be computed from the wrong clock

`framePeriodMs` comes from the measured display rate, falling back to the source
rate when that is unavailable - existing behaviour, reused. The two are equal at
23.976p so nothing looked wrong on the bench, but at 60 Hz output with 24p
content they differ by 2.5x, and a load percentage computed from the source rate
would read 16% for a frame using 40% of the refresh.

The provenance is now tracked and carried to the OSD and the log
(`frame_period_src=display|source`). When it is the fallback, the percentages
are **suppressed** rather than shown wrong, and the budget row says so. The
proper fix is `GetDetectedDisplayRefreshRate` from `codex/vp0169-present-timing`;
this change does not depend on that branch.

## Rebuild both halves together

The change adds a virtual to `IRenderer`, which changes the vtable that
`VideoProcessor-VPRenderer.dll` is called through. It bumps
`VP_LIBPLACEBO_PLUGIN_API_VERSION` 14 → 16 so a stale DLL is **rejected** rather
than called through a mismatched vtable. Host and plugin must be built from the
same tree.

## Files touched

| File | Change |
|---|---|
| `vprenderer/AlphaRenderLoadMeter.h` | new - time-based window, session peak, warm-up guard |
| `ProcessCpuUsageMeter.h` | new - process CPU usage and its session peak |
| `RendererLiveness.h` | `RendererRenderLoad` POD, beside `RendererLatencySnapshot` |
| `IRenderer.h` | `GetRenderLoad()`, defaulting to unsupported |
| `LibplaceboVideoRenderer.h/.cpp` | timer callback, meter, accessor, log fields |
| `LibplaceboPluginVideoRenderer.h/.cpp` | proxy forwarder for the new virtual |
| `LibplaceboRendererPluginApi.h` | API version 14 -> 16 |
| `StatsOverlayWindow.h/.cpp` | four OSD rows + panel height |
| `VideoProcessorDlg.h/.cpp` | populate from the renderer; drive the CPU meter |
| `VideoProcessor-Lib.vcxproj(.filters)` | the new headers |

No logic of the existing code is removed. Three lines change in place: the API
version constant, and the telemetry format string plus the last line of its
argument list, both because fields are appended to that same call.

## Known, not fixed here

`RollingPerformanceWindow`, duplicated in the four frame formatters, sizes its
window as `600 samples // 10 seconds @ 60fps` - really 25 s at 23.976p, so the
existing `10s Avg/Max` conversion row is mislabelled on 24p content. Folding
those four copies into one shared window is the natural follow-up, but it sits
on the capture/conversion hot path and does not belong in this change.
