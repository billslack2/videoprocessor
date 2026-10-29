# Reading the render-load stats

Only the new Prep/New/Reuse CPU/wait rows and their processing-window
measurements are temporarily disabled (`OsdTimingPolicy::ProcessingPerformanceEnabled`).
The existing GPU frame, CPU/GPU output load, source/output interval, conversion,
queue/drop and cadence statistics remain enabled.

The Ctrl+I panel reports a rolling ten-second view:

```text
GPU frame:       avg 2.10, max 2.62 ms (10 s)
10s avg/max: CPU 12/34%, GPU budget 16/20%
Source interval: 41.71 ms @ 23.976 fps
Output interval: 13.33 ms @ 75.000 fps
```

GPU frame sums bounded GPU timestamp stages belonging to one successfully
submitted output: source upload, changed overlay upload, core render, and any
timed subtitle stages. Gaps between stages, CPU conversion, Present waiting,
and scanout are excluded. Some GPU preparation, including temporal-history copies
and native-field preflight, runs outside these intervals, so this is not complete
pipeline GPU cost or end-to-end processing latency.
Average and maximum describe accepted samples in the trailing ten seconds.
Timing alternates with the legacy pass diagnostic, so the maximum is the maximum
sampled cost; it is not a guarantee that every frame was timed.

Source interval describes incoming frames, using measured capture cadence with
known input-format fallback. Output interval describes the scheduled render
cadence and remains the GPU-budget percentage denominator. With 23.976 fps input
and smooth motion rendering at 75 Hz, these intervals are 41.71 ms and 13.33 ms.
When temporal processing is inactive, the output interval uses source/render
cadence; a fast monitor merely scanning out the same image does not reduce it.
These intervals are cadence references, not guaranteed deadlines for every stage.

GPU budget percentages divide each sampled submission by its own saved output
interval. Source upload may be present on the first output and reused by later
outputs, so the maximum can reflect a source-only burst rather than sustained
output load. This is sampled render-budget use, not GPU hardware-engine occupancy.
Average and maximum percentages are tracked independently of maximum milliseconds.
During an ordinary reset, source interval falls back to the known input format;
output budget uses a valid temporal schedule when available, then source/render
cadence. If neither is valid, percentages are suppressed. Completed GPU history
and the established D/R estimate survive a reset; restart or real contract/pipeline
changes begin a new measurement session.

CPU is processor time charged to all VP threads, divided by elapsed time and
logical processor count. Its average weights each interval by its overlap with
the last ten seconds; its maximum is the largest overlapping sample percentage.
For example, one saturated thread on 12 logical processors reads about 8%.

Available measurements appear immediately by default; unresolved values say
measuring. The shared OsdTimingPolicy::WarmingEnabled switch can restore optional
warmup. D/R estimate uses the beta's smoothed interval estimator and validated
startup display rates in both native and fallback paths.

Compare demanding content over a full rolling window after changing settings.
Late GPU results are telemetry delay, not an intentional delay to video.
Session peaks and core/legacy pass diagnostics remain in logs only.

Useful log fields include gpu_frame_avg_ms, gpu_frame_peak_ms,
gpu_load_avg_pct, gpu_load_pct, gpu_frame_coverage_pct, gpu_source,
gpu_submission, gpu_lag_frames, gpu_unmatched, gpu_query_failures,
gpu_disjoint, gpu_overruns, and gpu_lock_drops. Process CPU load reports
the rolling average and maximum every ten seconds. render_ms and swap_ms
remain CPU wall-time diagnostics, not GPU execution or process CPU use.
