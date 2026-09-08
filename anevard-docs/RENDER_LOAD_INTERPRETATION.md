# Reading the render-load stats

The Ctrl+I panel reports a rolling ten-second view:

```text
GPU frame:       avg 2.10, max 2.62 ms (10 s)
10s avg/max: CPU 12/34%, GPU budget 13/16%
Frame budget:    16.68 ms @ 59.941 fps
```

GPU frame is the sum of source upload, changed overlay upload, and core render
GPU timestamp intervals belonging to one successfully submitted frame. Gaps
between these stages, CPU conversion, Present waiting, and scanout are excluded.
Average and maximum describe accepted samples in the trailing ten seconds.
Timing alternates with the legacy pass diagnostic, so the maximum is the maximum
sampled cost; it is not a guarantee that every frame was timed.

GPU budget percentages divide each sample by its own validated source/render
period. At 23.976 fps the budget is approximately 41.71 ms, even when the monitor
scans out repeated frames at a higher rate. This is frame-budget utilization,
not GPU hardware-engine occupancy. Average and maximum percentages are tracked
independently of maximum milliseconds. Invalid cadence suppresses percentages.

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
