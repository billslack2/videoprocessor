# VP-0024: Alpha source-to-display timing and queue telemetry

## Status

Draft. Diagnostic-only foundation for VP-0026 and VP-0027. The design is based
on source review and retained logs; implementation must first confirm the exact
DXGI frame-statistics behavior of the bundled libplacebo 7.360.1 D3D11
swapchain on the target Epson path.

Supersedes VP-0005 and VP-0017 and the investigative portion of VP-0008.

## User story

As an Alpha-renderer user, I want every captured frame to be correlated with
VP queueing, rendering, DXGI submission, and actual presentation so queue
depth, latency, clock drift, drops, repeats, and presentation glitches have
one unambiguous meaning.

## Established pipeline model

Alpha currently has one bounded CPU FIFO:

`capture callback -> m_frameQueue -> format/upload/render -> libplacebo/DXGI`

`GetFrameQueueSize()` reports only `m_frameQueue`. The active frame has already
been removed, `GetConvertedQueueSize()` is always zero, and submitted or
in-flight swapchain frames are not counted. DXGI device configuration permits
up to two frames in flight. Consequently `R/C/T` is normally `R/0/R`, and a
VP depth of zero does not prove that the presentation pipeline is empty.

The render loop is paced by `pl_swapchain_start_frame`,
`pl_swapchain_submit_frame`, and `pl_swapchain_swap_buffers`; capture
timestamps do not schedule presentation.

## Scope

- Alpha/libplacebo only; do not change DirectShow behavior.
- Add measurement, correlation, labels, and rate-limited logging only.
- Do not add prefill, queue trimming, sleeps, drops, repeats, or renderer
  resets.
- Treat the Epson projector path as the authoritative tuning environment.
  Desktop-monitor runs are adverse/mismatch tests, not tuning evidence.

## Required design and implementation

1. Assign a monotonically increasing renderer generation and source sequence
   to each accepted frame. Record capture timestamp and enqueue time.
2. Record dequeue time, queue depth and oldest-frame age at the same locked
   sampling boundary, conversion/render duration, submit time, swap-block time,
   present result, and source-buffer release reason.
3. Unwrap the Alpha DXGI swapchain and evaluate:
   - `IDXGISwapChain::GetLastPresentCount`;
   - `IDXGISwapChain::GetFrameStatistics`;
   - `PresentCount`, `PresentRefreshCount`, `SyncRefreshCount`, and
     `SyncQPCTime`;
   - `IDXGISwapChainMedia::GetFrameStatisticsMedia` where available and useful.
4. Maintain a bounded ring mapping source sequence to capture timestamp,
   submit QPC, DXGI present ID, and observed presentation refresh/time. Never
   retain a source buffer merely for telemetry.
5. Estimate the display cadence from the slope of refresh count versus QPC
   over a stabilized window. Keep Windows target-path rate, measured DXGI
   cadence, and capture cadence as separate values.
6. Handle first-sample and mode-change
   `DXGI_ERROR_FRAME_STATISTICS_DISJOINT`, renderer rebuilds, monitor changes,
   counter wrap, unavailable statistics, bitblt presentation, minimization,
   and multiple-monitor ambiguity explicitly. Invalid evidence must disable
   correction readiness rather than silently fall back to a guessed rate.
7. Expose a coherent Alpha snapshot containing:
   - queued, actively rendering, submitted/in-flight, and last-presented state;
   - current/high-water/capacity and oldest outstanding age;
   - source-to-present sequence debt;
   - capture and measured display rates with stability/validity;
   - queue-pressure drops, render failures, empty-queue waits, swap failures,
     and detected presentation glitches as separate counters.
8. Replace Alpha's `R/C/T` presentation with one renderer-native queue line,
   such as current/desired depth plus frame age. The internal hard safety limit
   and DXGI in-flight count may appear in expanded diagnostics, but must not be
   presented as additional VP raw/converted queues. Keep detailed per-frame
   correlation in rate-limited logs.
9. Validate the DXGI interpretation against a PresentMon/ETW trace or another
   independent presentation trace without making that external tool a runtime
   dependency.

## Verification

- Exercise 23.976/24 and 59.94/60 on the Epson for long enough that both
  capture and display estimates are stable.
- Capture one healthy 0-1-depth run and one induced backlog/starvation run.
- Exercise startup, the one startup re-prime, renderer switching, mode changes,
  F2/F3 profile changes, display-rule rebuilds, hidden/minimized state, and
  recovery.
- Prove that every retained correlation record is bounded and generation-safe.
- Confirm telemetry does not measurably change render time, queue depth,
  latency, or frame disposition.

## Acceptance criteria

- A trace identifies which source frame reached which DXGI presentation and
  refresh interval.
- Display cadence is measured from presentation statistics with an explicit
  validity/stability state; nominal Windows mode is never presented as a
  physical measurement.
- Queue depth, active work, DXGI in-flight work, frame age, and sequence debt
  are distinct.
- Drops, repeats/holds, render failures, and presentation glitches cannot be
  conflated.
- Normal Alpha behavior is unchanged.

## Dependencies and follow-ups

Unblocks VP-0026 and VP-0027. VP-0025 may proceed independently but must use
the same source sequence and renderer-generation contract.
