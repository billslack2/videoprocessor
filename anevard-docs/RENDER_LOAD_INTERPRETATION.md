# Reading the render-load stats

The Ctrl+I rows answer whether the VP Renderer shader pipeline has comfortable
GPU headroom at the active display refresh rate.

```
GPU render 10s:  avg 4.27, worst 4.85 ms, max 29%
GPU budget:      16.68 ms @ 59.941 Hz
GPU session:     worst 5.31 ms, max 32%
CPU process:     now 3%, peak 6%
```

- `GPU render 10s` is the mean recent render-pass cost and the highest share of
  a refresh period seen in the rolling ten-second window.
- `GPU budget` comes from the same monitor-qualified display rate used by the
  rest of the OSD. A source-frame period is never presented as a display budget.
- `GPU session` is the highest load since the current shader/profile pipeline
  settled. A live NLS or profile change clears it and starts another settling
  period, so old and new pipelines are not mixed.
- `CPU process` is VideoProcessor's process-wide CPU usage, comparable to Task
  Manager's whole-machine percentage.

## Important timing semantics

The GPU figures are libplacebo's native per-pass timer results. libplacebo
resolves its D3D11 timestamp queries asynchronously and reports the latest
available result for each pass. Values may lag by several frames or be absent
temporarily. They are therefore accurate recent render-pass costs, but they are
not an exact timing ledger for the frame currently being presented.

VideoProcessor does not add D3D11 timestamp queries, nest disjoint intervals,
call `Flush`, poll for completion, or wait on the GPU. Doing any of those to
force same-frame attribution would perturb the latency being measured.

The measurement covers libplacebo render passes. It does not claim to be total
GPU occupancy for uploads, presentation, unrelated processes, or work running
concurrently on other GPU engines. Use a GPU profiler when that distinction
matters.

## Comparing settings

After playback starts or a shader/profile changes, wait until `settling...`
disappears. Then compare the ten-second average and maximum using the same
content and display mode. A single maximum near 100% means there is little or no
headroom for that workload, but it does not by itself prove the corresponding
present missed its deadline because timer delivery is asynchronous.

The window and session percentages are paired with the display period that was
active when each GPU sample was recorded. This prevents a refresh-rate change
from applying today's budget to an older millisecond peak.

`render_ms` and `swap_ms` remain diagnostic log fields only. They are CPU wall
times around calls and can include driver back-pressure or display pacing; they
must not be added to the GPU number or interpreted as shader execution time.

## Log fields

`Alpha presentation telemetry:` includes `gpu_recent_estimate_ms`,
`gpu_avg_ms`, `gpu_peak_ms`, `gpu_load_pct`, `gpu_load_ms`,
`gpu_load_period_ms`, `gpu_passes`, and corresponding session fields. The line
ends with `timer_semantics=libplacebo-recent-async` to make the attribution
limit explicit. `display_target_hz` is the monitor-qualified budget authority;
the separate DXGI `display_hz` measurement is rejected if its cadence conflicts
with that target.
