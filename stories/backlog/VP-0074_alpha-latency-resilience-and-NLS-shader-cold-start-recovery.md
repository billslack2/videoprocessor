# VP-0074: Alpha latency resilience and NLS shader cold-start recovery

## Status

Backlog. Created 2026-08-02 as an independent successor to VP-0069's
uncompleted end-to-end latency investigation. No implementation branch has
been created.

## Problem

The first runtime activation of the NLS GLSL hook on 2026-08-02 compiled on
Alpha's live render thread: approximately 798 ms GLSL-to-SPIR-V plus 176 ms
HLSL-to-DXBC. Capture continued, the Alpha queue grew from its target of 2 to
28 frames, and the OSD correctly reflected roughly 459 ms of queued-frame age.
The picture was correct and the steady-state renderer was fast, but the delay
remained sticky until a manual renderer reset cleared and re-primed the queue.

## User story

As an Alpha user, I want enabling an optional shader such as NLS not to leave
video hundreds of milliseconds behind live after its first compile.

## Scope

1. Establish whether the qualified NLS shader variant can be compiled/prewarmed
   before live frame delivery, including renderer/profile transitions.
2. If a live compile or another bounded render stall still occurs, detect the
   resulting backlog and safely flush/re-prime Alpha to its configured
   `[queue] target_frames` without showing stale frames.
3. Preserve the accepted NLS image result, lossless P210 ingress, explicit P010
   fallback, color metadata, and normal steady-state presentation behavior.
4. Retain CPU v210-to-P210 unpack. Raw-GPU v210 is not in scope unless a later
   physical measurement proves a material end-to-end benefit.

## Required evidence

1. Log per-shader cold/warm compile duration, renderer generation, queue
   depth/oldest age, and the selected recovery decision.
2. Test first and repeated NLS activation at 23.976 and 59.94/60 Hz in
   fullscreen and embedded/windowed presentation where relevant.
3. Prove no retained backlog after the bounded compile/recovery path; the queue
   returns to the configured target without sustained drops, stale frames, or
   incorrect OSD delay.
4. A/B measure `[queue] target_frames`, D3D11 frame-latency behavior, and the
   DXGI presentation model only when they offer a material whole-frame benefit.
5. Record physical capture-to-target evidence separately from VP/Presents;
   retain a change only for a roughly 20 ms or greater net improvement, or a
   documented reliability benefit with no latency regression.

## Non-goals

- No blanket GPU-pipeline rewrite or GPU-to-CPU readback.
- No shader disablement as a silent workaround.
- No claim of physical 50 ms capture-to-photon latency without an external,
  repeatable qualification measurement.

## Dependencies

- VP-0069-1 (Done), for accepted native ingress and Alpha telemetry.
- VP-0024 and VP-0026, for source-to-display timing and queue behavior.
