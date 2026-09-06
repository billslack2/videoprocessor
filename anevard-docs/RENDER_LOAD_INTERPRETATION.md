# Reading the render-load stats

The Ctrl+I additions answer one question: how much of the measured display
refresh is consumed by VP Renderer's GPU work?

```text
GPU 10s:         avg 4.42, peak 6.71 ms (16%)
GPU budget:      41.71 ms @ 23.976 Hz
GPU session:     peak 7.94 ms (19%)
CPU process:     now 12%, peak 34%
```

`GPU 10s` shows the average and worst accepted end-to-end VP GPU interval in
the latest ten seconds. The interval begins with source upload and ends after
final rendering; it includes clear and changed OSD texture uploads but excludes
Present, DWM composition, and scanout. Its percentage uses the peak, because
one over-budget frame can be visible even when the average is low.

`GPU budget` is one measured display refresh. At 23.976 Hz it is about 41.71
ms; at 59.94 Hz it is about 16.68 ms. If display timing is unavailable, VP says
so and suppresses percentages rather than substituting the source period.

`GPU session` is the worst accepted interval since this renderer instance
started, excluding warm-up. It does not decay with the ten-second window. A
peak percentage retains the measured display period from that same submission,
so a later refresh-rate change cannot reinterpret an older peak.

`CPU process` is actual processor time charged to all VP threads, expressed as
a share of the whole machine. On 12 logical processors, one fully occupied
thread is roughly 8%.

## Measurement delay versus video latency

The GPU number normally arrives a few frames after the frame was successfully
submitted.
That is telemetry delay, not video latency. VP never waits for the result. Each
duration retains its original generation, source sequence, and unique
submission serial; a result that cannot be matched exactly is discarded.

## How to compare settings

1. Let the OSD leave `settling...`.
2. Change one quality setting at a time.
3. Compare peak with peak over representative demanding content.
4. Check the session peak before ending the run.
5. Record the display refresh rate with the result.

The same millisecond cost consumes a larger percentage at a higher refresh
rate. Treat percentages as measured headroom, not a universal pass/fail score;
driver contention and unrelated GPU work can also affect a frame interval.

## Log fields

Useful GPU fields on `Alpha presentation telemetry:` include:

| Field | Meaning |
|---|---|
| `gpu_ms`, `gpu_avg_ms`, `gpu_peak_ms` | newest, average, and ten-second peak |
| `gpu_load_pct` | ten-second peak divided by measured display period |
| `gpu_session_peak_ms`, `gpu_session_pct` | renderer-session peak |
| `gpu_source`, `gpu_submission` | identity of the newest accepted result |
| `gpu_lag_frames` | issued-frame delay before that result resolved |
| `gpu_segments` | timestamped GPU operation groups in that submission |
| `gpu_pending`, `gpu_resolved` | asynchronous query-ring health |
| `gpu_not_ready` | nonblocking polls whose result was not ready yet |
| `gpu_disjoint`, `gpu_query_failures` | invalid driver timing evidence |
| `gpu_rejected`, `gpu_overruns` | failed submissions and no-wait ring loss |
| `gpu_unmatched`, `gpu_invalid` | stale identity and invalid duration loss |
| `gpu_warmup` | exactly matched samples intentionally excluded by warm-up |
| `frame_period_src` | `display` when percentages are trustworthy |

`render_ms` and `swap_ms` are CPU wall time around calls. They can primarily
represent back-pressure or waiting for vsync and must not be added to GPU time
or interpreted as shader cost.
