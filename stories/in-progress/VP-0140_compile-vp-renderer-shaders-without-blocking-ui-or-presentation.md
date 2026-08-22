# VP-0140: Compile VP Renderer shaders without blocking UI or presentation

## Status

In Progress (2026-08-21). Source worktree:
`C:\\Videoprocessor\\vp\\vprenderer\\.codex-worktrees\\vp-0140`, branch
`codex/vp-0140-nonblocking-shader-preparation`, based on
`v1.2.001-beta` at `a31803d`.

Initial inventory confirms that UI resize and display-change already coalesce
through nonblocking try-lock paths, but cold `pl_render_image` work still runs
on the presentation worker while it owns `renderMutex`. The first implementation
slice introduces and tests generation-aware preparation scheduling (one active
request plus the newest compatible pending request) without moving libplacebo
calls across an unsafe D3D11 context. The next slice will bind that scheduler to
a renderer-owned compatible compilation context and cache handoff.

Initial source commit: `8fa4cbf` (`VP-0140 add shader preparation scheduling
boundary`). `VideoProcessor-Test` x64 Release built successfully; focused
coalescing/supersession and retirement-publication tests passed.

## User story

As a VideoProcessor operator, I need every potentially cold VP Renderer shader
compilation to run independently of both the UI thread and the live
presentation thread, so shader selection, renderer switching, window resizing,
restart, and exit remain responsive while a replacement pipeline is prepared.

## Problem statement

VP invokes libplacebo rendering from a render worker, but that alone does not
make compilation nonblocking. A lazy compile can occur inside
`pl_render_image` while the worker holds the broad renderer mutex. UI-facing
resize, display-change, reset, shader-status, and lifecycle operations can wait
for that mutex, and synchronous stop/retirement paths can wait for the render
thread itself. The deployed telemetry captured cold compilation exceeding five
seconds, which is long enough to stop live presentation, freeze the visible UI,
and prevent a prompt close.

The required invariant is stronger than "not called directly by the UI": no
UI or live-presentation operation may synchronously wait for shader source
generation, translation, driver compilation, pipeline creation, or a worker
performing those operations.

## Scope

1. Inventory every VP-controlled libplacebo compilation trigger, including
   initial activation, explicit shader preparation, shader/profile selection,
   renderer re-entry, output-size or scaling-ratio changes, output-format
   changes, and cache misses. Distinguish inexpensive per-frame shader graph
   construction and cache lookup from cold source translation, driver
   compilation, and pipeline creation.
2. Introduce a dedicated, renderer-owned compilation service and libplacebo
   compilation context. It must respect the selected backend's thread-safety
   contract and must not concurrently mutate the live `pl_renderer`. If the
   active `pl_gpu` cannot safely support independent compilation, use an
   isolated compatible device/context and a synchronized compiled-cache
   handoff.
3. Make UI-facing resize, display-change, shader-selection, reset, stop, and
   close paths enqueue versioned intent and return promptly. They must not take
   a mutex held across compilation and must not synchronously join the compiler
   or presentation worker.
4. Keep presenting with the last known-good pipeline while a replacement is
   compiling. When its exact target geometry is not reusable, use a bounded
   generic resizable/blit fallback rather than suspending presentation.
5. Debounce and coalesce resize-driven preparation. Associate work and results
   with renderer, configuration, shader, source-format, output-contract, and
   target-geometry generations so superseded results can never activate.
6. Activate a successfully prepared pipeline atomically at a frame boundary.
   A failed compile must retain the last known-good path, report the failure,
   and remain retryable without blocking UI or presentation.
7. Make shutdown two phase: request cancellation/retirement immediately, then
   join workers and destroy compiler/GPU/cache resources through background
   retirement. Cache load, update, save, and destruction must have explicit
   ownership and cannot race an in-flight compile.
8. Add thread, generation, queue, compile-duration, activation, cancellation,
   and fallback telemetry sufficient to prove where each phase executed and
   why a result was applied or discarded.

## Acceptance criteria

1. A deterministic test gate can hold a cold compilation for at least ten
   seconds while the VP window continues to process paint, move, resize,
   command, and close messages. No UI handler waits for the gate, the live
   renderer mutex, or a compiler-thread join.
2. During that gate, the presentation path continues submitting the last
   known-good output or the documented generic fallback; there is no
   compile-duration-sized presentation outage.
3. Instrumented thread identities prove cold GLSL processing, SPIR-V/HLSL
   translation, driver compilation, and pipeline preparation never execute on
   the UI or live presentation thread. Ordinary per-frame graph construction
   and cache lookup remain bounded and are reported separately.
4. At least 100 rapid resize or shader/profile requests retain no more than one
   in-flight preparation plus the newest pending compatible request. Only the
   newest compatible generation can activate, and stale completions are logged
   and discarded.
5. With the same VP/libplacebo binary, adapter, driver, configuration, and
   valid persistent cache, returning to a cached fullscreen or windowed
   variant avoids cold compilation. Cache updates survive restart without
   corrupting or replacing a newer cache generation.
6. Compile failure leaves the UI responsive and presentation active on the
   previous/fallback pipeline, exposes a concise operator-visible status, and
   does not force a renderer restart unless the GPU itself is failed.
7. Closing or switching renderers while compilation is pending returns control
   to the UI immediately. Background retirement cancels or drains the work,
   joins safely, releases all references, and cannot publish into the retired
   renderer generation.
8. Automated concurrency tests cover resize/compile, selector/compile,
   reset/compile, renderer-switch/compile, and close/compile races under both
   successful and failed compilation.
9. A successful x64 Release build passes the focused tests. Deployment
   validation reproduces cold-cache fullscreen/windowed and
   VP Renderer-to-madVR-to-VP Renderer transitions while confirming responsive
   UI, uninterrupted fallback presentation, safe close, and authoritative
   telemetry.

## Design constraints

- Do not implement this as `std::async(pl_render_image(...))` against the live
  renderer. Mutable libplacebo renderer state, GPU/backend thread-safety, cache
  ownership, swapchain access, and cancellation must remain explicitly owned.
- Do not trade steady-state image quality for responsiveness. A generic scaled
  frame is permitted only as a temporary preparation fallback; the final
  stable presentation must use the configured direct-to-target quality path.
- Do not clear or flush reusable compiled shader state during ordinary resize,
  renderer re-entry, or profile changes unless compatibility evidence requires
  invalidation.
- Compilation workers should run below UI priority and must not hold VP global,
  renderer-lifecycle, presentation, queue, or UI-state locks while invoking the
  compiler or graphics driver.

## Boundaries and dependencies

- VP-0134 remains responsible for symmetric renderer handoff, renderer
  ownership, and restoration of display-global state. VP-0140 integrates with
  that lifecycle but does not redefine its ownership contract.
- VP-0139 and VP-0128 own VP Renderer quality-policy presentation and broader
  libplacebo option parity. This story preserves their selected quality and
  addresses only nonblocking preparation and activation.
- This story covers VP-controlled libplacebo/VP Renderer compilation and
  preparation. It cannot guarantee where madVR performs its private internal
  compilation, but VP-controlled DirectShow shader preprocessing must remain
  off the UI thread and must not reintroduce synchronous lifecycle waits.
