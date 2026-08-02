# VP-0074: Alpha latency resilience and NLS shader cold-start recovery

## Status

Review. The implementation is ready for live user validation on branch
`codex/vp-0074-alpha-latency-recovery`, commits `8867cfb` and `9eb7198`,
rebased onto the current `v1.1.015-beta` tip `44e3099` (VP-0023 Alpha P010
formatter contract). The clean worktree is
`C:\Users\bslac\vp\worktrees\vp-0074`.

The exact `9eb7198` x64 Release build (`VERSION_DIRTY=false`) passed all 471
native tests and was deployed to `C:\Videoprocessor\vp` on 2026-08-02. The
executable, Alpha plugin, and `shaders\NLS.glsl` deployment hashes match the
build outputs. `VideoProcessor.cfg` was not changed.

Remaining review: exercise embedded and fullscreen Alpha at 23.976 and
59.94/60 Hz, including NLS off/on, normal/scope profile changes, and window
resize. Confirm the startup-prewarm records are successful or warm, an aspect
change does not produce a new NLS compiler event, and any genuinely new-size
compile is followed by one bounded backlog recovery to `[queue] target_frames`.

## Readiness review

- The active configuration model was checked against the current loader. The
  `[shaders] rules` list supplies all fixed NLS constants; the swapchain owns
  the final output pixel dimensions, and detected active-picture geometry owns
  the runtime stretch ratio.
- The renderer pipeline and resource lifetime are known: configured rules can
  be parsed during renderer initialization, but qualification must wait until
  the renderer has a real input frame and swapchain target. Temporary prewarm
  hooks are destroyed after the normal/scope variants are rendered behind the
  startup cover; the persistent GPU shader cache remains renderer-owned.
- The clean worktree was based on the then-current GitHub default branch
  `v1.1.015-beta`. Queue-policy behavior has deterministic rate-scaled unit
  coverage; GPU compilation and presentation behavior require the live Alpha
  validation listed above.

## Investigation evidence

The 2026-08-02 deployment log showed several distinct triggers with the same
sticky-backlog result:

- 15:12:42: initial pipeline work consumed about 879 ms GLSL-to-SPIR-V,
  23 ms SPIR-V-to-HLSL, and 202 ms HLSL-to-DXBC. The alternate screen-profile
  prewarm then consumed about 1.06 seconds. The queue reached 32 and remained
  around 31 frames / 509 ms.
- 16:27:51: first live `NLS.glsl` activation consumed about 798 + 22 + 176 ms.
  The queue moved from its target of 2 to 28 frames / 459 ms and did not drain.
- 16:32:31 and 17:24: window-size changes generated new output-size pipelines;
  the queue retained roughly 15-26 frames until a manual reset.
- 17:21: provisional active-picture bounds changed the literal NLS stretch
  ratio repeatedly, manufacturing extra shader variants during an aspect
  transition.

The producer and vsync consumer have the same long-term rate. Consequently,
once any render-thread stall adds frames, ordinary playback has no spare
throughput with which to drain them; the elevated queue latency is sticky even
after rendering returns to 30-40 ms.

## Implemented

1. Alpha measures the complete render cycle, including render-mutex waits. If
   a rate-scaled render stall or stale-frame age occurs while queue depth is
   above the configured target, it drops only the oldest same-generation
   frames, keeps the newest target-sized reserve, and resets cadence evidence.
2. Compiler telemetry now records cold/warm status, all three compiler-stage
   durations, input/output size, renderer and queue generations, queue depth,
   oldest-frame age, and the recovery decision.
3. Alpha now enumerates every configured libplacebo NLS rule at renderer
   startup and prewarms each unique fixed rule for the real swapchain size in
   both normal and scope profiles before normal presentation.
4. NLS stretch ratio and warp axis are libplacebo `DYNAMIC` parameters. Active
   picture/aspect changes update GPU parameter data and no longer change shader
   source or require compilation.
5. A provisional active-picture candidate no longer replaces the previously
   published stable geometry until it earns crop authority.

## Build and deployment evidence

- Clean x64 Release rebuild: passed; embedded commit `9eb7198`,
  `VERSION_DIRTY=false`.
- Native tests: 471 passed, 0 failed, including queue-recovery thresholds at
  23.976 and 59.94/60-rate families.
- Deployed SHA-256: executable
  `5C0607965D6CC900277B5766488B13E272C83039E9D4A867BCDFF0EC8837D5B8`;
  Alpha plugin
  `B6A9378417E802C11A3296CC35D303CDF78E44287D99FAD045F4919D142A44B7`;
  NLS shader
  `36D8A96083F2194C5FCB880931A2529B7F962770041CF67A75019F3CDBDD57A2`.
- Rollback files use suffix
  `.before-9eb7198-20260802-184300.bak` beside each deployed artifact.

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
