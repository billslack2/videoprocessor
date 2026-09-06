# Render-load OSD stats

This change adds diagnostic GPU frame time and process CPU use to the Ctrl+I
OSD. It does not change the rendered picture, presentation order, queue depth,
or any non-OSD UI.

## Goal

Show whether VP Renderer quality work fits inside one measured display refresh,
without making the measurement itself wait for the GPU or adding a video frame
of latency.

## GPU measurement

VP owns a fixed ring of 16 D3D11 query sets. Each set contains:

- a timestamp immediately before `pl_render_image`
- a timestamp immediately after `pl_render_image`
- the enclosing `D3D11_QUERY_TIMESTAMP_DISJOINT` query

The timestamp interval measures the GPU timeline occupied by commands issued
for that render call. It includes barriers and GPU contention inside the
interval, which is intentional: those costs also determine whether the frame
fits its refresh budget. It does not include `Present` or CPU-side vsync waits.

Every query slot carries three provenance values:

- renderer/queue generation
- source sequence
- unique successful-submission serial

The successful submission record is created immediately. A GPU duration is
attached later only when all three values match that exact record. This also
distinguishes cadence repeats that render the same source sequence more than
once. It proves which submitted source frame was measured; physical scanout
remains separate DXGI presentation telemetry.

The following evidence is discarded and counted instead of entering an
average or peak:

- render or submission failures
- timestamp-disjoint intervals
- failed, zero, reversed, non-finite, or implausibly long query results
- results whose presentation record was cleared or expired
- new frames skipped for timing while the bounded ring is full

This replaces summing `pl_render_info.pass->last`. That callback reports the
latest resolved value for a pass, not a value identified as belonging to the
current source frame, so its results cannot form an exact per-frame ledger.

## No-wait rule

Query objects are allocated once during renderer initialization. During
rendering VP:

1. polls only the oldest query, at least two issued frames later
2. passes `D3D11_ASYNC_GETDATA_DONOTFLUSH`
3. stops polling as soon as the oldest query is not ready
4. skips timing the new frame if the ring is still full, preserving all
   unresolved queries

There is no `Flush`, sleep, spin, or blocking `GetData` loop. GPU results are
therefore intentionally a few frames late in the OSD, but video is not held for
them. Telemetry records `gpu_source`, `gpu_submission`, and `gpu_lag_frames` so
that delay is observable.

The measurement still has small, non-zero command and polling overhead. The
libplacebo per-pass info callback is left null, avoiding its sample-history copy
and VP's former per-pass mutex, but on-hardware A/B measurement is still needed
before claiming zero performance impact.

## Statistics

Accepted samples feed:

- average over a 10-second time window
- peak over that 10-second window
- peak over the renderer session

Warm-up samples are excluded for at least three seconds after renderer start or
timing reset. A backlog recovery clears the recent window and re-arms warm-up;
the renderer-session peak survives so the recovery cannot erase the evidence
that motivated it. Constructing a new renderer creates a new session.

Load percentages are calculated only when the period came from measured display
timing. A source-rate fallback may be useful elsewhere, but it is not a valid
display-frame budget and produces no percentage in either the OSD or log.

## OSD

The four compact rows fit the existing 540-pixel panel:

```text
GPU 10s:         avg 4.42, peak 6.71 ms (16%)
GPU budget:      41.71 ms @ 23.976 Hz
GPU session:     peak 7.94 ms (19%)
CPU process:     now 12%, peak 34%
```

When measured display timing is unavailable, the budget row says
`display timing unavailable` and no percentage is shown.

CPU use comes from `GetProcessTimes` and represents VP's share of the whole
machine. It is sampled on the existing one-second UI timer whether or not the
OSD is visible, and resets when the renderer/host generation changes. Opening
the OSD therefore neither starts measurement nor averages one long hidden
interval.

## Telemetry and compatibility

The existing `Alpha presentation telemetry:` line retains its earlier fields
and appends window, session, provenance, query-health, and discard counters.
`render_ms` and `swap_ms` remain CPU wall-time diagnostics and are not displayed
as GPU cost.

`GetRenderLoad` changes the renderer interface, so the plugin API version is
bumped and the host and `VideoProcessorVPRenderer.dll` must be built from the
same source tree.

## Verification status

The x64 Release library, GUI, and renderer plugin build successfully. Automated
tests cover exact generation/source/submission matching, stale results after a
reset, repeated source sequences, invalid duration rejection, and suppression
of source-rate percentages. Hardware A/B timing and visual inspection remain
required because unit tests cannot measure driver overhead or scanout.
