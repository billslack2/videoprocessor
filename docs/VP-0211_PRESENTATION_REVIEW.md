# VP-0211 presentation review and qualification

2026-10-25. Baseline: beta `b1415da4`; prior candidate: PR #198 / `10a4aa9a`.
Three independent Luna xhigh reviewers researched mpv, libplacebo and regressions.
This report distinguishes API/source evidence from actual display qualification.

## Source comparison and decisions

| Concern | Primary-source behavior | VP decision |
| --- | --- | --- |
| Queue depth | mpv D3D11 and bundled libplacebo set device maximum frame latency and allocate depth plus one flip buffers. The setting is a cap, not observed occupancy. | Expose 1..8, default 2; explicitly check setter/getter, use applied depth plus one buffers, leave capture Queue independent. |
| Timing | mpv display-sync uses interval-1 Present with queued-frame bookkeeping and optional DXGI timing feedback. Initial Presents can enqueue before later backpressure. | Remove the draft CPU sleep timer. Use one render/context owner, interval-1 Present and a next-unsubmitted-slot estimate. Never treat a Present return as a physical-vblank callback. |
| Feedback | mpv estimates the last queued display time as SyncQPC + ((last Present ID - displayed Present ID) - (SyncRefreshCount - PresentRefreshCount)) periods, with documented ambiguity. | Use wrapping unsigned deltas, cap/age bounds, nonzero fields and multiple consistent refresh/QPC samples. Reject past-submit, disjoint, implausible and unsupported samples; adjust only the wall anchor. Keep FIFO fallback. |
| Source clocks | mpv's display-sync retimes file playback and may resample audio. VP is live capture without an audio clock. | Preserve healthy source PTS. Use one normalized scheduler domain for peeking/accepting broken timestamps; do not retime capture to the display or copy mpv's audio policy. Raw capture ticks remain recorded. |
| Repeated output | Renderer output can be cached instead of repeating source processing. | Own final GPU pixels and source/geometry metadata; repeat without retaining capture storage or advancing source/subtitle analysis. |
| Temporal output | Interpolation/deinterlacing determines temporal pictures separately from swapchain submission. | Keep nominal deinterlace outputs, repeat final pictures between fields, and use the same slot authority. Drop obsolete temporal outputs after a stall instead of catch-up rendering. |
| Backend results | libplacebo D3D11 submit consumes the acquired frame; its void swap_buffers method separately calls Present(1,0). Unwrap returns an AddRef'd swapchain. | Submit through the wrapper, then make exactly one checked native Present with an owned COM reference. Do not also call swap_buffers. Both ownership paths distinguish accepted Present from failure/occlusion. |
| Waitable chains | Waitable DXGI chains use their own latency setting; device latency does not apply. At depth greater than one a signaled handle is not a vblank event. | Retain existing non-waitable model and bounded Present backpressure. No new wait handles, second GPU/context thread, forced depth-one mode or timer competing with Present. |

Primary references:

- [mpv D3D11 at 14f2d48cbc7dda61adb4bd181e107a1f3f76e533](https://github.com/mpv-player/mpv/blob/14f2d48cbc7dda61adb4bd181e107a1f3f76e533/video/out/d3d11/context.c),
  [VO loop](https://github.com/mpv-player/mpv/blob/14f2d48cbc7dda61adb4bd181e107a1f3f76e533/video/out/vo.c),
  [gpu-next](https://github.com/mpv-player/mpv/blob/14f2d48cbc7dda61adb4bd181e107a1f3f76e533/video/out/vo_gpu_next.c),
  [options](https://mpv.io/manual/master/#options-swapchain-depth).
- Bundled `3rdparty/libplacebo/source/libplacebo-7.360.1-vp-source.zip`,
  `src/d3d11/context.c` and `swapchain.c`; the bundled 7.377.0 VP source was
  also checked for the same depth-plus-one and interval-1 contracts. [Upstream swapchain API](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/swapchain.h),
  [D3D11 backend](https://github.com/haasn/libplacebo/blob/master/src/d3d11/swapchain.c),
  [plplay example](https://github.com/haasn/libplacebo/blob/master/demos/plplay.c).
  plplay separates source-PTS timing from submit/swap; it is not an every-refresh
  scheduler or a reason to add a second timing loop to VP.
- Microsoft [device latency](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgidevice1-setmaximumframelatency),
  [Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present),
  [frame statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics),
  [waitable chain guidance](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains).

## Reviewer concerns addressed

- Removed independent sleep_until pacing that could fight blocking Present.
- Added verified depth control/buffer alignment rather than treating source FIFO
  depth or three backbuffers as presentation-depth configuration.
- Unified queued/dequeued timestamp fallback; frozen/backward/nonfinite/jumping
  PTS cannot silently bypass source gating. Unestimable front timing disables
  repeat pacing instead of permanently holding the source FIFO.
- Restored nominal deinterlace work: the old forced-display-rate change could
  exceed the temporal output cap and bypass deinterlacing (20 fps at 360 Hz).
- Cached final temporal output and metered fields/repeats through one slot clock;
  retired obsolete outputs after stalls, preserving first actual source analysis.
- Consumed acquired libplacebo frames exactly once and checked native Present;
  failed repeat acquisition retires the cache/clock. Preserved COM/GPU lifetimes.
- Made telemetry explicit about successful submissions, unknown occupancy and
  guarded feedback; no universal smoothness or madVR equivalence claim.

## Bounded hardware smoke probe

Source: [presentation_queue_probe.cpp](../tools/presentation_queue_probe.cpp).
Results: [summary](VP-0211/queue-probe.jsonl),
[per-frame API/timing samples](VP-0211/queue-probe-frames.csv).
A small non-activating test window used the current hardware adapter and desktop
mode; no deployed VP files, active configuration or display modes changed.
The program used the bundled libplacebo D3D11 GPU and both native-created wrapped
and libplacebo-created swapchains. Each combination submitted 120 frames with an
injected 40 ms CPU pause; total 720 Presents. The earlier 48-frame probe was
exploratory and is not added to these totals.

All six combinations applied/read back depths 1, 2, 8; buffer counts were 2, 3, 9.
All 720 Presents returned S_OK. Median Present blocking was about 15.9 ms in the
sampled desktop mode. Per-frame CSV retains startup/full-cycle/acquire/submit
cost and the pause; medians exclude the first 12 calls. This is evidence for
API integration and backpressure, not measured input-to-output latency.

Crucially, each chain returned success for 119 statistics calls after an initial
disjoint result, but all returned timing/count fields were zero. The production
feedback guard accepted none. A successful statistics HRESULT is insufficient
phase evidence. These windows did not establish physical displayed/discarded
outcomes, exact queue occupancy, HDMI cadence or mode-switch behavior.

Reproduce from a Visual Studio x64 Developer PowerShell at the source root:

```powershell
New-Item -ItemType Directory -Force tmp/vp0211-probe | Out-Null
cl /nologo /EHsc /O2 /MD /std:c++17 /I3rdparty/libplacebo/include /Isrc/VideoProcessor-Lib tools/presentation_queue_probe.cpp /Fetmp/vp0211-probe/queue_probe.exe /Fotmp/vp0211-probe/queue_probe.obj /link /LIBPATH:3rdparty/libplacebo/lib libplacebo-360.lib d3d11.lib dxgi.lib user32.lib
$env:Path = (Resolve-Path 3rdparty/libplacebo/bin).Path + ';' + $env:Path
& tmp/vp0211-probe/queue_probe.exe
```

## Remaining live qualification

Match ordinary/temporal playback at 23.976/24/50/59.94/60 inputs, same/mismatched
refreshes including 120 Hz, both presenter owners and depths. Measure source
continuity, actual display outcomes, configured queue reserve, end-to-end delay,
missed outputs under render/CPU/GPU stalls, mode/profile/LUT transitions and
shutdown. Define durations/tolerances before comparing. A bounded FIFO can cover
only its available lead, not arbitrarily slow rendering. Queued old frames can
drain after a reset; holding the last image is distinct from new submissions.
No live-capture/HDMI qualification or deployment is claimed by this report.

## Build and regression evidence

The full x64 Release solution built successfully with the official external
Magewell SDK root; the final candidate build reported 10 existing conversion
warnings and zero errors. All 373 selected native regressions passed in 19.8 s,
covering slot/source clocks, fractional cadence, GPU pixel reuse, temporal output,
configuration/public-reference validation, source queue, reset and presentation
telemetry policy. The offscreen Display controls default/save/reopen test passed.
The source comparison and regression reviewers' remaining issues were corrected;
final Luna regression review approved the reviewed paths subject to live
qualification above. `git diff --check` and local documentation links passed.

The first build exposed an inherited PowerShell 7 module path in Windows
PowerShell post-build hashing. A build-local PSModulePath containing the Windows
PowerShell module folders resolved it; no product or global environment setting
was changed. The first regression pass found a missing documented numeric value;
the reference was corrected before the complete passing run.
