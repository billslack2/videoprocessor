# VP-0069-2: Alpha end-to-end latency reduction investigation

## Status

Will Not Do. Parent: VP-0069. Closed 2026-08-02.

This follows VP-0069-1's lossless native ingress work. It is deliberately
deferred until physical capture-to-target telemetry identifies a material
whole-frame opportunity. It does not authorize a raw-GPU v210 conversion.
Its remaining work is superseded by standalone root VP-0074 so VP-0069 can
close as the native-ingress delivery tranche.

## User story

As an Alpha-renderer user, I want measured reductions in capture-to-target
latency without compromising the proven lossless P210 ingress, tone mapping,
or presentation stability.

## Decision and scope

- Keep the CPU v210-to-P210 unpack. It preserves every captured 10-bit 4:2:2
  sample, keeps the GPU focused on tone mapping/scaling/presentation, and does
  not introduce a shader-format or analysis-correctness risk for a small CPU
  saving.
- Do not implement raw-GPU v210 merely as an optimization. Reconsider it only
  if physical A/B measurements demonstrate at least roughly 20 ms net
  capture-to-target improvement, with no regression in color correctness,
  renderer time, present stability, or the applicable analysis contract.
- Investigate only plausible whole-frame sources: `[queue] target_frames`,
  D3D11 `max_frame_latency`, and the DXGI presentation model
  (composed/BitBlt versus flip/direct). Change one factor at a time.
- Include disruptive runtime shader work. On 2026-08-02, the first NLS GLSL
  hook compile spent about 798 ms translating GLSL to SPIR-V and 176 ms
  translating HLSL to DXBC on the live render thread. The Alpha queue then
  rose from its target of 2 to 28 frames (about 459 ms oldest-frame age).
  Evaluate prewarming the qualified NLS variant, or a safe flush/re-prime after
  a known compile stall, before considering any new rendering design.

## Required evidence

1. Establish a repeatable physical baseline at 23.976 and 59.94/60 Hz using
   the common DeckLink v210 source. Record Alpha OSD `Delay: Total`, renderer,
   presentation, queue age/depth, render time, and swap-block telemetry.
2. A/B `[queue] target_frames` values at a stable `queue_size`; distinguish the
   hard queue capacity from the maintained live target.
3. A/B each presentation and D3D frame-latency candidate independently across
   fullscreen and windowed presentation where both are applicable.
4. Measure the cold and warm NLS enable path. A candidate must avoid retained
   backlog after compilation and preserve the accepted steady-state image.
5. Validate SDR and HDR/tonemapped output, refresh changes, rebuilds, queue
   recovery, drops, and visible stability for any candidate that reduces delay.
6. Retain only changes that improve physical capture-to-target delay by about
   20 ms or more, or provide a separately documented reliability benefit with
   no latency regression.

## Non-goals

- No raw-GPU v210 shader, GPU-to-CPU readback, or broad GPU-pipeline rewrite
  without the evidence above.
- No compromise to source sample accuracy, color metadata, tone mapping,
  scene/black-bar behavior, or subtitle behavior to obtain a benchmark-only
  saving.
- No change to the VP-0069-1 native-ingress acceptance work.

## Dependencies

- VP-0069-1, for the lossless v210/P210 capture path and Alpha telemetry.
- VP-0024 and VP-0026, for existing source-to-display timing and queue
  observability.
