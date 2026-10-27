# Dual-monitor scheduler qualification

This note records the retained log evidence and a manual qualification matrix for the dual-monitor playback report. It does not change the live configuration. The operator reported that playback is currently smooth after setting `present_every_vsync=false` and restarting VP; preserve that working state unless a deliberate future test is arranged.

## Retained evidence

The timestamps below are local time (America/New_York) from `C:\Videoprocessor\vp\logs\vp.log*`, inspected on 2026-10-27. No video frames or private source content are included here.

| Window | Emitted pacing traces | Renderer queue counters | Display-repeat / presentation evidence |
| --- | --- | --- | --- |
| 2026-10-26 21:27–21:32 | 35 traces, all `reason=submit_gap`, `cadence_repeat=0`; sampled gaps 29.212–32.816 ms against a 29.195–29.196 ms threshold. Callback gaps were about 16.5–16.9 ms. | The 5-second `Alpha pre-render timing` summaries rose from `renderer_dropped=605`, `capacity_drops=0` at 21:27:03 to 719 and 18 at 21:31:48. `cadence_drops=0` and `cadence_repeats=0` throughout. | 15 `VP display refresh` samples from 21:29:24–21:29:54 show display rate about 59.9409 Hz, `feedback=1`, `pts_fallback=0`, `queued_sources=8`, `source_analysis=unchanged`; `cached_repeats` rose 613→694 and `retired_sources` 605→686. The issue notes identify this session's presentation owner as display-repeat and the API call as `Present(1)`. |
| 2026-10-26 21:34–21:39 | 2 traces, both `reason=display_refresh_gap`, at 21:35:53 and 21:37:43. Gaps were 16.753 and 16.931 ms against a 29.195 ms threshold; callback gaps were about 16.685 ms and preview waits 11.963 and 12.335 ms. Both had `cadence_repeat=0`. | All 60 `Alpha pre-render timing` summaries report `renderer_dropped=0`, `capacity_drops=0`, `cadence_drops=0`, and `cadence_repeats=0`. | Presentation telemetry reports queue depth 8, oldest queued age around 117 ms, and source/presentation debt of 2–3. The issue notes identify this session's owner as source and the API call as `Present(1)`. No `VP display refresh` counter rows occur in this window. |

The first window's cumulative renderer removal counter increased by 114; the 35 emitted pacing traces are not a drop count. The `renderer_dropped=719` value at the end matches the operator-reported UI total, but that numerical match alone does not establish the UI's counting semantics or prove 719 physical scanout losses. In the implementation, `renderer_dropped` counts queue removals, `capacity_drops` counts hard queue overflow, and the cadence counters are separate. The display-refresh path increments `cached_repeats` when it presents cached output and increments `retired_sources` when backlog recovery removes a queued source frame. In the observed excerpt, both display-refresh counters rose by 81.

The `Alpha frame pacing trace` logger emits at most one qualifying trace every two seconds. It records a `submit_gap` when submit time exceeds the source period plus 0.75 display periods, or `display_refresh_gap` when one DXGI present-count step spans more than the expected refresh count. Therefore, trace counts and the absence of a trace are samples, not complete event totals. `Present(1)`, DXGI statistics, renderer counters, and subjective UI observations do not independently establish what the panel physically scanned out. An optical measurement is needed to make that claim.

These two retained sessions show different queue and display-repeat counters, but they do not by themselves isolate monitor placement, second-monitor workload, refresh pairing, or the setting change. Treat them as observations, not a controlled causal A/B result.

## Manual qualification matrix

Run this only as a planned future test while preserving the currently working state. Use the same VP build and renderer DLL, one repeatable local clip with a known frame rate, the same five-minute excerpt and starting PTS, the same VP window size, and the same renderer options for every trial. Do not seek, resize, open menus, or switch content during a measured interval. Change `present_every_vsync` only between trials and restart VP normally so each run has a known setting. Record the exact setting used; do not infer it from a log symptom.

Cover the 16 combinations below (two settings × two refresh pairings × two VP monitor placements × two workloads on the other monitor). Use the two existing displays and record their identities, physical arrangement, primary designation, resolution, and exact reported refresh rates for every run.

| Factor | Values |
| --- | --- |
| `present_every_vsync` | `false` (current reported working setting); `true` (comparison setting) |
| Refresh pairing | Matched: both displays at the same available nominal rate, preferably the observed 59.94 Hz pair; mixed: the actual production refresh-rate pair. Record exact rates; do not label a nominal 60 Hz mode as exactly 60.000 Hz. |
| VP placement | VP on display A; VP on display B. Keep primary designation and window geometry fixed within each pairing. |
| Other-display workload | Idle desktop; a repeatable, fixed-size moving workload such as the same local test clip. Keep the workload and foreground behavior identical across paired runs. |

For each combination, allow 30 seconds to settle, then measure five continuous minutes. Alternate paired `false`/`true` runs within each topology where practical. If a difference appears, repeat that pair before calling it a result. A clean single-monitor run can be added as a control when the dual-monitor result remains ambiguous.

Record before and after each measured interval, with generation/session identity and timestamps:

- `renderer_dropped`, `capacity_drops`, `cadence_drops`, and `cadence_repeats` from `Alpha pre-render timing`; compute deltas rather than comparing lifetime totals.
- `cached_repeats` and `retired_sources` from `VP display refresh`, when that path logs them; record whether those rows are absent rather than treating absence as zero.
- `source`, `presented`, `debt`, `present_id`, `refresh`, `frame_stats_hr`, `queue_after`, `oldest_ms`, `render_ms`, and `swap_ms` from `Alpha presentation telemetry`.
- Every emitted `Alpha frame pacing trace` with its `reason`, gap/threshold, callback/enqueue/pre-render waits, queue depth/age, present-stat deltas, and statistics-validity fields. These are rate-limited samples, not event totals.
- The UI's displayed drop total and human-visible stutter separately from renderer counters. If the result must assert physical repeats or missed scanout, add an independent optical recording and synchronize it with the VP log; otherwise report that scanout was not measured.

Compare matched conditions by counter deltas, queue depth/oldest age, debt trend, emitted pacing reasons, and visible playback. A useful pass has no new hard capacity overflow, no sustained growth in queue latency or debt, and no repeatable visible stutter relative to the matched baseline. Interpret cadence-related counts against the source and display rates; do not assume a counter named “repeat” or “drop” is a one-to-one physical frame-loss measurement.
## Waitable swap-chain assessment

Defer a waitable-swap-chain change from this repair. A DXGI frame-latency waitable object controls downstream rendering workahead; it does not make a queued source eligible under VP's capture-buffer reserve policy or decide whether that source timestamp is due. It is not a VBlank clock or independent scanout proof. Keep `Present(1)` and investigate source admission first.

If later measurements demonstrate excess workahead or presentation blocking after the scheduler repair, evaluate an opt-in windowed flip-chain experiment. It requires the creation flag, `IDXGISwapChain2::SetMaximumFrameLatency`, a wait before rendering (including the first frame), cancellation during shutdown/recovery, and correct handle lifetime across recreation. Preserve the flag during resize. Cover both libplacebo-owned and VP-owned paths explicitly; D3D11 exclusive fullscreen cannot use this flag. Compare wait time, rendering cost, Present blocking, queue age, source retirement, and visible playback under the same source/topology.

Primary references: [DXGI waitable-chain guidance](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains), [GetFrameLatencyWaitableObject](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject), [per-chain latency](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency), and [swap-chain flag restrictions](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_chain_flag).

## Automated reproduction and candidate repair

The candidate separates cached-repeat eligibility from capture queue admission. Previously, `!CanDequeueLocked()` could authorize a cached repeat even when the queue head's timestamp was already due: the queue reserve or conversion readiness gate was mistaken for a future-frame decision. The repaired predicate permits a repeat only for an empty queue or a finite, future, non-cadence-repeat head. A due blocked head waits for normal queue readiness without an extra Present. Queue reserve, source speed, DXGI sync interval, and the `present_every_vsync=false` path are unchanged.

`MatchedNear60HzLowWaterDoesNotCreateRepeatsOrRetirements` exercises 600 source presentations with a 59.94097 Hz source, 60000/1001 Hz display, quantized PTS, callback jitter, depth-three capture reserve, depth-two workahead, and stale-source retirement. The unchanged production predicate, extracted into a testable helper before repair, produced 32 cached repeats and 29 retired sources. The repaired predicate produces zero of each. Additional assertions retain empty/future-head repeats and reject due, cadence-repeat, and nonfinite heads. Existing film cadence, clock drift, feedback, and recovery tests remain applicable.

Validation completed on 2026-10-27: the original baseline passed 155 scheduler/queue/recovery/motion tests; the repaired isolated x64 Release test build passed all 75 selected clock, queue-policy, and cadence-recovery tests. Red and green TRX records and binaries are retained locally in ignored `bld/test-red`, `bld/test-red-results`, `bld/vsync-green`, and `bld/vsync-green-results`. A coordinating reviewer independently reviewed the production change and tests without finding an actionable issue.

This is a deterministic source-admission/slot-clock reproduction. It does not exercise actual DXGI feedback, threaded capture, physical scanout, or dual-monitor topology. It is consistent with the retained incident's repeat/retirement pattern but does not prove the cause of every historical drop. The manual matrix above is not yet run. No deployment or live configuration change was made for this candidate.

Final validation: the x64 Release host, stable renderer, beta renderer, and native test targets built successfully. All 156 selected clock, queue, cadence-health/recovery, presentation-config, and motion tests passed against that final build (TestResults/vsync-final/vsync-final.trx). The v143 configuration utilities and output probe also built separately with their installed toolchain; the initial single-toolchain solution invocation could not build those v143 projects under VS2019.
