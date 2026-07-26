# VP-0005: Alpha renderer timing observability and queue health

## Status

Will Not Do. Superseded on 2026-07-25 by VP-0024, which expands the proposed
diagnostics into source-to-display correlation using Alpha-owned DXGI present
statistics. Retain this record for its original observability rationale; do not
implement it separately.

## Context

The experimental in-process libplacebo renderer is implemented in:

`C:\Users\bslac\vp\videoprocessor - VS2026\src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`

It already has a dedicated render thread, a bounded `m_frameQueue`, a DXGI/
libplacebo swapchain, entry/exit latency measurements, and a capture-cadence
PPM estimate. Its cadence estimate is diagnostic-only. When its queue reaches
the configured limit, `OnVideoFrame` drops the oldest queued frame.

Before adding corrective timing behavior, establish whether real playback needs
it and whether the source of any drift is capture cadence, render throughput, or
presentation blocking.

## User story

As an alpha-renderer tester, I need concise, rate-limited logs and OSD-accessible
metrics that explain queue pressure and presentation timing, so I can determine
whether a visible skip was an intentional queue drop, a render failure, or a
display-pacing issue.

## Scope

Add observability only. Do not introduce manual sleep pacing, scene analysis,
frame repeats, renderer restarts, or changes to `Start()` / `Stop()` lifecycle
behavior.

## Implementation plan

1. Add renderer-native counters/atomics for:
   - queue high-water mark and configured limit;
   - oldest-frame drops caused by queue pressure;
   - render failures separately from queue-pressure drops;
   - rolling render-plus-submit/present duration;
   - time spent waiting for `pl_swapchain_swap_buffers`, if it can be measured
     around the call without changing behavior.
2. Preserve the current existing generic dropped-frame counter, but expose or
   log the reason-specific values so users can distinguish them.
3. Emit a rate-limited diagnostic line (for example, every 10 seconds while
   active, and immediately on a pressure drop) containing:
   - measured capture rate and PPM deviation;
   - current/high-water/limit queue depth;
   - pressure-drop and render-failure totals;
   - rolling render and swap/present durations;
   - current display refresh rate and active presentation model.
4. Add only compact alpha-specific OSD data if the existing renderer statistics
   interface supports it cleanly. Do not clutter the OSD with a second timing
   panel; logs are the authoritative diagnostic surface for this story.
5. Reset counters at the same boundaries as the renderer's normal stream reset
   or start, and clearly log that reset.

## Verification

- Build x64 Release and run the existing test suite.
- Test stable matched-rate playback for at least 15 minutes: queue remains
  bounded, counters are coherent, and no behavior changes from the baseline.
- Intentionally constrain queue depth or render load to produce pressure drops:
  each drop has one unambiguous logged reason.
- Exercise fullscreen/window transitions, display refresh switches, F2/F3 screen
  profile changes, and renderer restart. Metrics must reset or remain monotonic
  according to documented behavior and must not cause a crash.

## Acceptance criteria

- No correction policy is introduced.
- Existing lifecycle and presentation code paths are unchanged apart from timing
  measurement and logging.
- A log review can distinguish capture drift, queue pressure, slow presentation,
  and rendering failure without guesswork.
