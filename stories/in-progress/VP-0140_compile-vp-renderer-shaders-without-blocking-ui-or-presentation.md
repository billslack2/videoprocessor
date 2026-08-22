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
boundary`). Follow-up commit: `4c99fd6` (`VP-0140 add dedicated shader
preparation worker`). The worker owns cancellation, coalescing, completion,
and background join semantics without holding a renderer/GPU/UI lock while it
executes work. A deterministic gate proves requesting-thread responsiveness,
worker-thread execution, newest-request promotion, stale completion discard,
and retirement-safe publication. It deliberately does not yet dispatch
libplacebo work; the next slice creates a compatible isolated D3D11/libplacebo
context and hands its serialized cache result back to the live renderer.

`VideoProcessor-Test` x64 Release built successfully; focused coordinator and
worker tests passed. Incremental test linking can emit `LNK1103` in this
worktree due to corrupt debug information; a normal non-incremental Release
link regenerated the test DLL and passed validation.

Follow-up commit: `9925eee` (`VP-0140 prepare shader worker execution
context`). The dedicated worker initializes an MTA apartment, runs below normal
priority, and publishes its thread identity and priority with each completion.
The focused gate test again passed after an x64 Release rebuild. Inventory also
confirmed that `/prepare_shaders` already has the required synthetic
SDR/PQ, windowed/fullscreen, display-profile, and viewport-profile coverage;
the isolated in-process compiler context will reuse that contract rather than
inventing a different shader key.

Follow-up commit: `85ca85a` (`VP-0140 run shader preparation out of process`).
The explicit Config `Prepare shaders` command always launches the existing
non-capturing `/prepare_shaders` host at below-normal process priority. It no
longer cycles live VP Renderer profiles or runs prewarm `pl_render_image` calls
on the presentation worker. The GUI x64 Release build passed. This resolves
the explicit-preparation trigger; ordinary live selector, resize, output, and
startup cache-miss preparation still require the isolated context/cache-handoff
slice.

Deployment (2026-08-21): deployed the successful x64 Release GUI build from
`85ca85a` to `C:\\Videoprocessor\\vp\\VideoProcessor.exe`; SHA-256
`080DCABDC81141D4F9E0331E2809A4A3008357AA4ED312F3BAF1AFCA00E51DB9`.
The previous executable is preserved as
`VideoProcessor.exe.bak-20260821-234001-vp0140-external-prep`. No configuration
file was modified. Live cold-cache, resize, renderer-switch, and close
validation remains pending the isolated-context implementation.

Follow-up commit: `b6691e6` (`VP-0140 make shader compilation status visible`).
The non-activating renderer cover now displays a distinct shader-compilation
splash, forces a DWM composition boundary before the render worker enters the
driver, and reference-counts overlapping renderer workers so an older compile
cannot hide the splash while a newer compile continues. The callback remains a
fast posted UI message; failures to post and invalid targets now log explicit
diagnostics. The x64 Release GUI build passed with zero warnings and errors.
This improves the cold-cache experience and visibility only: the separate
isolated-context/cache-handoff slice remains necessary to keep old video
presenting through a brand-new shader compilation.

Deployment (2026-08-21): deployed the successful x64 Release GUI build from
`b6691e6` to `C:\\Videoprocessor\\vp\\VideoProcessor.exe`; SHA-256
`E20A96FB55013EC9C82C24D6F2A0C897E69EA1E7389871CD69E0ECC5EE550756`.
The previous executable is preserved as
`VideoProcessor.exe.bak-20260821-235513-vp0140-splash`. No configuration file
was modified.

Follow-up commit: `4d287c3` (`VP-0140 prepare configured shaders without
blocking UI`). Startup now gates only live VP Renderer construction while an
isolated `/prepare_shaders` process reads the active configuration and fills
the persistent cache. The main window continues capture and message dispatch
and shows a non-activating, input-transparent preparation screen until the
worker exits. With the deployed configuration, the worker enumerates both
display profiles, both viewport profiles, SDR and PQ sources, and windowed and
fullscreen targets (16 base candidates); it re-arms every configured NLS
prewarm rule for each candidate so NLS+ is not consumed only by the first
geometry.

Live display/profile and viewport updates now use a coalesced full-settings
snapshot and `try_lock`; when libplacebo is compiling, the UI call returns
immediately and the render worker applies the newest snapshot before its next
frame. Rec.709/BT.2020 and viewport changes explicitly arm the compilation
status before `pl_render_image`, fixing the missing splash on F5/F6 and
geometry-driven cold variants. The splash is a single serialized state rather
than a stale callback reference count. Config also distinguishes a completed
full-preparation timestamp from a cache file modified afterward.

Validation (2026-08-22): clean x64 Release builds succeeded for
`VideoProcessor-GUI`, `VideoProcessor-VPRenderer`, and
`VideoProcessor-Config`; all 867 `VideoProcessor-Test` tests and the direct
Config UI test suite passed. Deployed the clean `4d287c3` GUI executable and
renderer DLL with SHA-256 values
`6B0C1904EAEE688FB1E9CC4D819701016FF4947AF77BE3A4485B92107A579085` and
`CE68C1B5F056028F68607FC225D506E57286D439F0792B25C3DCE8E9BD31B613`.
The prior three binaries are preserved under
`C:\\Videoprocessor\\vp\\backups\\vp-0140-4d287c3-20260822-0026`.
The running Config process was not interrupted; its new executable is staged
as `config\\VideoProcessorConfig.exe.pending-vp0140`. No configuration file
was modified. Live operator validation remains pending before moving the story
to Review.

Correction and simplification (2026-08-22): deployed logs established why the
same NLS+ workflow had worked normally before this story. At 16:55 the
persistent cache contained 42 objects (5.58 MB), and F6/BT.2020 rendered warm
in 0.85 ms without compilation. The cache was then explicitly cleared at
23:45:43 and again at 00:08:27. Its replacement contained only 10 objects; the
first F6 transition compiled one missing composed libplacebo program for about
3.4 seconds and saved object 11. NLS+ itself was unchanged. The regression was
therefore cache-lifecycle handling and over-eager preparation/status behavior,
not a new requirement that NLS+ recompile for every window, fullscreen,
viewport, or color-profile transition.

Follow-up commit: `47b5d55` (`VP-0140 simplify shader cache lifecycle`). A
completed current cache now starts immediately without preparation or a
splash. Missing, explicitly cleared, failed, partial, or legacy-stale cache
lifetimes run the configured 16-candidate matrix once in the isolated
`/prepare_shaders` process while the main UI continues dispatching messages.
The completed status carries a versioned explicit-clear policy: later
append-only cache saves remain valid, while Config and the renderer both
invalidate status when Clear is requested. Legacy status uses the cache/status
timestamps only once for migration, preventing a repeated-preparation loop.

Live F5/F6, fullscreen, and viewport changes no longer display compilation
status merely because their geometry or profile changed. A 150 ms delayed
watchdog remains silent for a warm `pl_render_image`; if that call is actually
slow, it posts the input-transparent splash without waiting on the UI thread,
then hides it when rendering returns and logs whether compile telemetry was
cold or warm. The unused in-process coordinator/service and their speculative
tests were removed. The final change removes 622 lines and adds 213.

Validation and deployment (2026-08-22): clean x64 Release builds succeeded for
GUI, VP Renderer, and Config from clean commit `47b5d55`. A full
non-incremental native test build passed all 865 tests, and the direct Config
UI suite passed, including explicit proof that Clear deletes completed
preparation status. The paired deployed hashes are
`611B4DF2ECF529937545FD9402480A9ABB5FE91E0FCC52C7B9DB0498BFA0919C`
for `VideoProcessor.exe` and
`9B38E27862B16DD0285A625A19F8D269532DDF03BCBC007851EC1A56B6A0E321`
for `VideoProcessorVPRenderer.dll`; Config is
`56998A746C68952E87D2F2082B6C0222757FCA76EB7052768E286B0ED6D6DDB5`.
The replaced binaries and prior pending Config build are backed up under
`C:\\Videoprocessor\\vp\\backups\\vp-0140-47b5d55-20260822-0054`.
No configuration or cache file was changed during deployment. The known
partial 694,780-byte cache and older legacy status remain intentionally in
place, so the next launch must exercise one responsive startup preparation;
live operator validation remains pending before Review.

Failed live validation and correction (2026-08-22): the first `47b5d55`
startup proved that the application message loop remained responsive—madVR,
fullscreen commands, and shortcut dispatch continued—but VP Renderer was
correctly gated behind a long cold preparation and its splash was subsequently
hidden by another renderer/fullscreen transition. The splash did not refresh
from the status file, so the operator saw a black/logo preview with no visible
progress while candidates took 13--19 seconds each.

More seriously, closing/restarting the owning VP process only closed its child
process handle. It did not terminate the `/prepare_shaders` child. Live process
inspection found three preparation workers (`26968`, `10788`, and `4644`), two
with exited parents, concurrently writing the shared cache/status files. That
made Config's status non-authoritative during the run and left no owning VP UI
to transition into VP Renderer when an orphan finished. The backend eventually
did complete at 01:05:20, atomically saving 37 objects (5,347,336 bytes) and
then publishing `ready`, but the operator-facing completion path had already
been lost.

Corrective source commit: `0af7618` (`VP-0140 fix preparation ownership and
progress UI`). Preparation now serializes through one named mutex; a later
worker waits and coalesces if the active owner completes. Each child starts
suspended, is assigned to a kill-on-close job before running, and is explicitly
terminated as a fallback when its VP instance closes. Synthetic windowed and
top-level preparation HWNDs are never shown. While the exact startup child is
running and VP Renderer is selected, the one-second UI poll reloads the
worker's `Preparing ... N of 16` message and reasserts the input-transparent
splash if another state transition hid it. Selecting another renderer leaves
that renderer usable without the preparation cover.

All 865 native tests passed again and clean x64 Release GUI/plugin builds were
produced from `0af7618` with `VERSION_DIRTY=false`. The paired deployment
hashes are
`15E940E203C3E1B712F55CCCEAC4F9045D31ED33A36C9A8D73058FD24E84D81A`
for `VideoProcessor.exe` and
`9B38E27862B16DD0285A625A19F8D269532DDF03BCBC007851EC1A56B6A0E321`
for `VideoProcessorVPRenderer.dll`. The replaced pair is backed up under
`C:\\Videoprocessor\\vp\\backups\\vp-0140-0af7618-20260822-0113`.
The completed 37-object cache, active configuration, and running Config process
were preserved. A normal next launch must therefore skip preparation and start
VP Renderer immediately; another live operator validation remains required.

Final preparation rollback (2026-08-22): live validation showed that the
configured matrix was itself the regression. Its 16 entries were a synthetic
Cartesian product of two display profiles, two viewport profiles, SDR/PQ, and
windowed/fullscreen hosts—not evidence that libplacebo needed 16 distinct
programs or one program per window size. Individual candidates could take more
than 30 seconds, so exhaustive preparation was neither predictable nor useful.

Source commit `d351151` (`Remove exhaustive shader preparation`) deletes the
matrix and its complete control plane: the Config Prepare button/progress UI,
`/prepare_shaders` command, worker process/job/mutex ownership, request event,
status file, cache-lifetime preparation policy, startup renderer gate, NLS
prewarm enumerator/render loop, and preparation-only renderer/runtime APIs.
The Shaders Setup page now reports only persistent-cache size and retains the
explicit Clear action. VP Renderer never precompiles windowed, fullscreen,
viewport, color-profile, or source-state combinations. It compiles only a
pipeline reached by live playback and atomically merges a cold result into the
persistent append-only libplacebo cache.

The remaining live-miss contract is deliberately small. `pl_render_image`
runs on the presentation worker, not the MFC UI thread. Resize, display change,
shader selection, and live profile updates use `try_lock` plus coalesced pending
state, so UI-originated calls do not wait behind a driver compile. When a
live render remains slow for 150 ms, a watchdog uses only the callback's posted
UI message to show the non-activating, input-transparent compilation splash;
the worker hides it when the render returns. Warm cache hits remain silent.

Validation and deployment (2026-08-22): clean x64 Release builds succeeded for
GUI, VP Renderer, and Config. All 861 native tests passed, and the complete
Config UI test executable passed with an explicit assertion that the Prepare
control no longer exists. Deployed hashes are
`3C3D64FE6C393DA4FEB497CBAE774C0F0E8E5E992C33B3B10DE3D09CA20F03C2`
for `VideoProcessor.exe`,
`BB2D7EEB3FBA28E64F2C6F773395B04F1E3DC81B3AC2E0E65875301B18A3ED87`
for `VideoProcessorVPRenderer.dll`, and
`2CC1E2DC2AA61478775D390BFDCAA273E0B29327C86FDA9AE4C40AAFC616090A`
for Config. The replaced binaries are backed up under
`C:\\Videoprocessor\\vp\\backups\\vp-0140-d351151-20260822-0142`.
No configuration or shader-cache file was changed during deployment. Live
operator validation of a cold windowed start, fullscreen transition, and F6
BT.2020 selection remains required before Review.

Final simplification (2026-08-22): live validation of `d351151` showed two
remaining branch regressions. The delayed compilation overlay could remain
over video after a slow render, and VP Renderer explicitly replaced every
non-Off shader selection with `NLS: Fullscreen only` whenever its target HWND
was the normal embedded/windowed host. That restriction was not a libplacebo
requirement and directly contradicted the previously working windowed NLS
behavior.

Source commit `06f652d` (`Simplify VP Renderer shader cache lifecycle`) removes
the compilation-status callback, watchdog thread, posted UI message, status
painting, and fullscreen-only selector gate. Configuration exposes only cache
size and `Clear shader cache`; it contains no Prepare control or preparation
status. Exhaustive preparation remains deleted. Both windowed and fullscreen
renderers now apply the selected shader normally, and libplacebo compiles only
when live rendering reaches a genuine cache miss. UI-originated renderer
changes retain the existing `try_lock`/coalesced-intent paths rather than
waiting on a compile.

Verification and deployment (2026-08-22): the complete x64 Release solution
built successfully from clean commit `06f652d`. A forced non-incremental native
test rebuild passed all 861 tests, and the complete Config UI suite passed,
including assertions that the Prepare control and preparation status are
absent while explicit cache clearing remains. Deployed SHA-256 values are
`E60BCEA23855A26B0069A49D69B7AC0CD8D52356991197F67A151C66829C08FD`
for `VideoProcessor.exe`,
`2741E41BB16B3A2A57A2BC2F59C4FD57D3508EB2E44740860BE3828884FD7BB8`
for `VideoProcessorVPRenderer.dll`, and
`B853E6BCA836DF29462AE27852F2D722CF57CF6F8CE78EB479487EE88910F559`
for Config. The replaced binaries are backed up under
`C:\\Videoprocessor\\vp\\backups\\vp-0140-06f652d-20260822-0208`.
No configuration or shader-cache file was changed during deployment. Live
operator validation remains required before Review.

Custom lifecycle rollback (2026-08-22): windowed live telemetry after
`06f652d` proved that VP's remaining cache policy was still adding complexity
without controlling libplacebo's real program selection. A resize from
1040x585 to 2000x1125 selected a new composed program and spent about 5.45
seconds in GLSL/SPIR-V/HLSL/DXBC compilation, while returning to 1040x585 and
entering a previously used 2560x1440 fullscreen pipeline were warm. The shader
source was unchanged; the cold key was libplacebo's composed render program,
not a VP-authored NLS variant that could be usefully predicted.

Source commit `667c1cf` (`Remove custom shader cache lifecycle`) therefore
removes the last VP-authored persistence policy: no append-only on-disk union,
no unlimited cumulative cache, no re-read/merge on save, and no save on every
cold render. The cache is again a normal bounded libplacebo GPU cache (256 MiB
total, 64 MiB per object), loaded once when the renderer is created and saved
once when it is destroyed. Only the explicit clear-request marker remains as
VP lifecycle plumbing so Config can retain **Clear shader cache**. Preparation,
prewarm, synthetic variants, compilation splash/status, and transition-driven
cache work remain absent. The UI-thread-safe `try_lock` and coalesced pending
resize/profile/shader intents remain because they prevent UI callbacks from
waiting behind live rendering; they neither compile nor manage shader state.

Verification and deployment (2026-08-22): the complete x64 Release solution
built successfully from clean commit `667c1cf`. The forced non-incremental
native suite passed all 861 tests and the complete offscreen Config UI suite
passed. Deployed SHA-256 values are
`936DBE806733B5AC5B4E7001587373290AE5CA045021F660F36A4A7951947F16`
for `VideoProcessor.exe` and
`3A321654258F81A5FBDF1758A2A24E9EF5450738DC4F4335C587A4919BC3FB80`
for `VideoProcessorVPRenderer.dll`. The replaced pair is backed up under
`C:\\Videoprocessor\\vp\\backups\\vp-0140-667c1cf-20260822-0830`.
Config was left running and unchanged because this correction changes only the
renderer integration. No configuration or shader-cache file was modified.
Live operator validation remains required before Review.

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
