# VP-0140: Compile VP Renderer shaders without blocking UI or presentation

## Status

Review (2026-10-26). Current integration 37946697 is built, tested and deployed to the existing reconstruction trial. See the latest evidence below. Historical implementation: NLS and subtitle activation fixes are implemented and built in
`E:\codex\videoprocessor\nls-live-preparation-20261016`, branch
`codex/nls-live-preparation-20261016`, based on remote `v1.4.02-beta` at
`51b211dc0d1d45f9e65883ea07e1d1ad9a3a7e10` plus the latest local rational-clock,
analysis-performance, and subtitle/gradual-AR improvements. Source changes are
local and uncommitted; no merge or deployment has been performed.

NLS/NLS+ preparation now runs on a separate same-adapter D3D11 device with its
own renderer, hooks, and textures. Only the thread-safe libplacebo cache is
shared. Ordinary-fit video continues until the matching preparation succeeds;
the selected NLS effect then activates at a frame boundary. One active request
and the newest pending request bound work, stale completions cannot activate a
different selection, and preparation failure retains safe-fit rendering.
Snapshots follow the actual temporal/composition input contract, including
real mixer spacing and duration. Background retirement joins preparation before
destroying the shared cache or unloading its library.

Subtitle activation now uses its own isolated preparation worker for full-raster
normalization, cleanup, generated blur, and independent-alpha subtitle layers.
The requested mode is promoted only before TemporalRenderer.Begin at a capture
boundary, and every output in that capture uses the same mode. During pending
or failed preparation, full-raster video with original subtitles continues.
The key includes source identity, settings, actual mixing/deinterlacing contract,
and field order. A failed request can retry after disabling and re-enabling the
feature. Cue/blur geometry uses dynamic constants to avoid recompiling for each
caption size. Both NLS and subtitle preparation show **Preparing shaders...**;
subtitle preparation failures show a separate notice rather than claiming work
is still underway.

Validation: x64 Release host, stable renderer, beta renderer, and native test
builds succeeded. All **2,793 native regression tests passed** (zero failures),
including six preparation tests covering ten seconds of continued GPU video drawing
during preparation, latest-request activation, failure/retry, composition input
metadata, and cache reuse. The added subtitle GPU test covers SDR, temporal RGB,
and HDR native YUV/deinterlacing; varying cue sizes, blur radii, backgrounds, and
all/cleanup/layer phases produced no new shader-cache entries after preparation.
Separate stable/beta HDR bicubic GPU smoke tests
passed; subsequent live direct and 1/2/4/8-frame mixtures added no cache keys.
Three Luna reviewers reviewed the design; their concrete composition and mixer
coverage concerns were addressed. `git diff --check` passed.

Reproducible evidence and source/binary hashes are recorded in
`nls-test-results/subtitle-validation.json` in that source worktree, alongside
`subtitle-preparation.trx` and `subtitle-regression.trx`. Earlier NLS-only evidence
remains in `validation.json`, `nls-preparation-qualified.trx`, and `nls-regression.trx`.
The baseline local-source
fingerprint is `6e14aeca2ecea10d1a9ce0a45ee6e8bc92fd0b30773055e5fd341a5885b590df`.

Remaining acceptance: real-capture cold-cache NLS/NLS+ and subtitle switching at the user's
resolution/refresh rate, rapid selection changes, and renderer retirement.
Preparation still consumes some GPU resources; automated small-frame tests do
not establish full-resolution presentation latency. Other cold
rendering paths remain outside this scoped fix. Proposed next step is review
and requested live validation before merging or releasing; this does not renew
the broad historical acceptance below.

Historical acceptance (2026-08-22). The developer accepted the deployed x64 Release build at
source commit `7b72265` after live windowed/fullscreen, Rec.709/BT.2020,
renderer-switch, resize, cache-clear, NLS, and NLS+ testing. Source worktree:
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

Native shader-policy rollback (2026-08-22): live validation of `667c1cf`
captured a cold base libplacebo render while the configured shader was Off. A
window resize from 1040x585 to 1056x594 spent 10.66 seconds in GLSL-to-SPIR-V,
273 ms in SPIR-V-to-HLSL, and 2.66 seconds in HLSL-to-DXBC; the complete render
stall was 13.82 seconds. The same run logged `dynamic_constants=1`.

That setting was not libplacebo's selected quality preset. VP had begun
forcibly enabling it in `a31803d` as a speculative recompilation-avoidance
policy, which caused even shader-Off playback to use a VP-selected base shader
family. Source commit `a72a3ac` (`Stop overriding libplacebo shader constants`)
removes the override and its renderer-parameter plumbing. VP now copies the
native libplacebo render preset without changing `dynamic_constants`; a test
locks that invariant to the exported native preset.

The complete x64 Release solution built successfully and all 861 native tests
passed. Deployed SHA-256 values are
`4B29FF69767B5B084D60DAB4ED4B023B4BE6ED6D69E5B6F0B0697FFE84964D32`
for `VideoProcessor.exe` and
`4D7729DE18365411C38B6EAC6401077FE31F703AAC4A15919126F68D30853232`
for `VideoProcessorVPRenderer.dll`. The replaced pair is backed up under
`C:\\Videoprocessor\\vp\\backups\\vp-0140-a72a3ac-20260822-0845`.
The active configuration and 35-object libplacebo cache were preserved. Live
shader-Off resize validation remains required before Review.

Renderer-cache ownership rollback (2026-08-22): subsequent live validation
showed a previously exercised profile compiling again within the same operator
session. The remaining source audit found three VP calls to
`pl_renderer_flush_cache`: display-LUT failure, native stats-overlay topology
or bitmap-size changes, and native sweep-overlay topology or bitmap-size
changes. The overlay calls could discard every hot Rec.709, BT.2020, base, and
NLS program merely because OSD content changed. This contradicted
libplacebo's API contract, which says an explicit flush is normally unnecessary
even when image parameters, colorspace, or target configuration change because
libplacebo detects and replaces outdated resources itself.

Source commit `971fc1a` (`Stop flushing libplacebo renderer programs`) removes
every VP call to `pl_renderer_flush_cache` and the now-unused overlay
invalidation bookkeeping. libplacebo now exclusively owns its live renderer
program lifecycle. VP retains only bounded `pl_cache` persistence (load once,
save at renderer destruction) and the explicit user-requested cache clear;
ordinary profile, viewport, overlay, LUT, source-metadata, resize, and reset
changes cannot clear reusable renderer programs.

The x64 Release VP renderer plugin and GUI build completed successfully at the
clean `971fc1a` commit. All 861 native tests and the complete Config UI test
suite passed. Deployed SHA-256 values are
`8A80E2E5D03BA7F46E437B8C72E9E85F7934ECE358496F619084123533AD3DA3`
for `VideoProcessor.exe` and
`BEACC569889AA9BC4CFA5148AE654225B1BE1A9F3375AB54D59B983933275F71`
for `VideoProcessorVPRenderer.dll`. The replaced pair is backed up under
`C:\\Videoprocessor\\vp\\backups\\vp-0140-971fc1a-20260822-0919`.
`VideoProcessor.cfg` and the existing 57-object, 7,076,884-byte libplacebo
cache were preserved byte-for-byte. Live repeated F5/F6, NLS, OSD, and resize
validation remains required before Review.

Accepted final rollback (2026-08-22): the intervening shadow-render,
compilation-overlay, epoch, and shader-status implementations established that
VP could not safely predict or independently manage libplacebo's composed
render programs. Source commits `4e3cc38` (`Remove custom shader lifecycle
orchestration`), `b3a6783` (`Disable render stall overlay`), and `7b72265`
(`Remove unused shader overlay status mirror`) remove that speculative control
plane. VP no longer precompiles, guesses, flushes, tracks, or displays status
for shader variants. libplacebo owns normal on-demand program compilation and
its bounded persistent cache. VP retains only cache inspection and the
explicit user-requested **Clear shader cache** action.

The accepted UI-liveness boundary is the existing nonblocking `try_lock` and
coalesced-intent handling for resize, display/profile, and shader changes.
Those paths do not synchronously wait behind a render. The disabled overlay
code was left behaviorally inert for possible later investigation; its unused
status mirror was removed. This final scope deliberately does not claim a
second libplacebo renderer/device can prepare transferable programs in the
background or that cold compilation can continue presenting the old frame;
the attempted VP-level lifecycle was less correct and substantially slower
than libplacebo's standard behavior.

Final verification rebuilt the complete solution in x64 Release with
`VERSION_DIRTY=false` and passed all 861 native tests, including the real GPU
NLS/NLS+ path. All shader/cache/NLS Config tests passed. A separate
foreground/topmost timing test remained desktop-focus-sensitive on later
reruns and had already passed from the same unchanged Config test binary; the
final cleanup does not build into that test. The deployed executable SHA-256
is `C4F440FB7FF19BF17DDC0E4F5BBE85729AA42058805C54BD0A42C18BAC55D2FC`;
the replaced deployment is recoverable under
`C:\\Videoprocessor\\vp\\backups\\before-vp0140-7b72265-20260822-141308`.
No renderer DLL, configuration, or cache file was changed by that final
deployment. The completed feature branch is published at `7b72265` on
`origin/codex/vp-0140-nonblocking-shader-preparation`.

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

## Preparation wording and live-video follow-up - 2026-10-26

User requests Preparing shaders instead of Preparing video pipeline and asks
whether video can continue during compilation. Current fetched beta v1.4.02-beta
at 87f61f77d3d95ddbfc1eeae343501d763d49e80b still runs cold subtitle/core render
calls synchronously on the rendering thread. The stall status observes calls
lasting at least 500 ms; changing its text does not make compilation asynchronous.

Wording changed to Preparing shaders... in clean current-beta worktree
E:\codex\videoprocessor\shader-preparation-wording-20261026, committed/pushed as
98eea048 on codex/shader-preparation-wording-20261026. Exact single occurrence
replacement and git diff --check passed. This wording-only change has not been
built or deployed; the existing trial still contains its previous wording.

The isolated NLS/subtitle preparation implementation described above still exists
in nls-live-preparation-20261016, alongside substantial unrelated uncommitted work.
It has not been incorporated into current beta. Preserve that checkout. Bringing
its preparation changes forward requires selective porting against current
subtitle composition/motion and new stable_surround mode, followed by cold-cache
playback validation. The intended behavior remains: ordinary video with original
subtitles continues while a private worker prepares the requested effect, then
activation occurs at a frame boundary. This follow-up is an implementation
assessment, not evidence that current beta renders continuously during compilation.

## User authorizes integration after mockup review - 2026-10-26

User requests bringing background preparation forward, incorporating it into the
same reconstruction test deployment, and testing continued playback. Before
building/deploying, user requires review of an accurate animated UI mockup:
Preparing shaders with a subtle spinner, positioned/styled like top-left profile OSD.
Mockup: C:\Users\bslac\.codex\visualizations\2027\10\22\01a8bb95-df97-7332-97f0-83d3d73d12d5\shader-preparation-osd.html.
It shows a single-line compact notice alone or below a visible profile banner.
Source references: StatsOverlayWindow::RenderProfileChangesBgra and
NativeStatsOverlayPlacement::ResolveProfileSizing. Active scale_osd is true.
Preview is a 1080p top-left crop over placeholder video; browser font rasterization
is illustrative. Proposed spinner is a muted 14-reference-pixel ring, 1.6s rotation.
No application build/deployment performed for this request. Await visual review.
Next: selectively port isolated preparation onto fresh current beta, retain the
separate stable_surround configuration, implement agreed notice and test cold-cache
playback, rapid changes, retirement and failure before updating the same trial.


## Approved shared profile panel integration - 2026-10-26

User approved the mockup, then refined the design: preparation must be an entry
inside the existing profile panel, not a separate notice. Header Shaders, with
a subtle spinner and Preparing... as the value. Applies to all reported shader
preparation, including NLS and subtitles. Keep current profile changes alongside
it until preparation completes; never create two competing panels. The source
port preserves the separate stable_surround option. x64 Release build and
background worker/cache-handoff/playback regression validation are underway.
Target is the existing E:\codex\reconstruction-fill-options-trial-20261026 trial,
with config/state preservation and binary backups; main installation unchanged.

## Current-beta shared-profile preparation trial ready - 2026-10-26

Committed/pushed source: 810d8820fa0faa7f70b1330c4468f0f362b43345 on
codex/shader-background-preparation-20261026, freshly based on beta 87f61f77.
The accepted stable reconstruction remains a separate stable_surround option.
No merge or public release has been performed.

The existing profile banner now owns a Shaders column, with its existing small
header and value typography. Preparing... includes a subtle rotating indicator;
Unavailable is static on failure. NLS, subtitles and the general VP render-stall
status feed this shared column. Profile changes are coalesced in the same panel,
held during preparation, and resume normal fading after completion. The separate
centered loading popup is no longer used by this status path.

Background NLS/subtitle work owns a separate same-adapter device, renderer,
textures, hooks and metadata snapshots; only the thread-safe shader cache is
shared. Generation checks prevent superseded requests from activating. Retirement
joins work before destroying cache/device/library. Native-field preparation and
missing-neighbor BOB are covered. The old October 16 implementation's post-mix
NLS assumption was deliberately adapted to current per-source processing, and
subtitle preparation warms the final RGB-to-output graph before activation.

Validation: final clean x64 Release solution build passed. All 151 selected
native/GPU regressions passed (motion, subtitles, both original/stable fill,
ten-second continued drawing, stale-request rejection, failure/retry, interlaced
lookahead, exact cache reuse through final presentation, and shared banner
composition). Prior failing diagnostics are retained: the extended test initially
held a stale cleanup texture after helper recreation; corrected to use the current
texture. SDR, temporal RGB and native HDR composition now reuse prepared entries.

Existing trial updated in place:
E:\codex\reconstruction-fill-options-trial-20261026.
Runtime backup: backups\before-background-shaders-20261026-115656.
All existing CFG/STATE files matched their pre-update hashes, verified again after
the initial live run. Separate ShaderValidation.cfg/state copies were used for
playback testing; the main installation was not changed. Shader caches were
backed up before the cold-cache test.

Live capture (3840x2160, approximately 23.976 Hz; output approximately 59.95 Hz):
NLS prepared in 448 ms; initial cold subtitle preparation took 25.364 seconds.
Video continued during that interval: sampled two-second windows total 59.256
submissions/second, mostly 120 submissions per window. Later profile requests
prepared in approximately 175-219 ms. Logs showed preparation status sharing the
same native profile bitmap during live subtitle/NLS selection changes, then the
Shaders column disappearing when ready.

Limit: one profile-switch window had a 108.877 ms render peak and 106 submissions;
this is not a zero-stutter guarantee. Other internal cold rendering paths remain
outside the isolated NLS/subtitle work, though their reported preparation uses the
same status UI. The live trial was left available for user review; a subsequent
process from the same trial was observed running. Remaining acceptance is user's
visual assessment of shared layout/spinner and actual switching responsiveness.
Evidence: assets/VP-0140/shared-profile-preparation-810d8820-20261026/.

## Subtitle cycling and attached bar edge correction - 2026-10-26

User reported Ctrl+Shift+T sometimes exposing subtitles in the black bar, and
Black backing occasionally stopping about one pixel short of the bar.
Fresh current-beta worktree: E:\codex\videoprocessor\subtitle-cycle-bar-edge-20261026,
beta 87f61f77, with prior trial features carried forward. Committed/pushed tip
379466972611a30a9aeca376172b865501fccfcb on codex/subtitle-cycle-bar-edge-20261026.

Root cause found in preparation policy: the complete effective-settings key
revoked ready subtitle programs for dynamic style changes. Each cycle therefore
briefly entered the original-full-raster fallback, exposing native bar captions.
The key now excludes dynamic subtitle/viewport values while retaining source,
transport and processing identity. Subtitle-only changes also retain the NLS
preparation revision. Initial cold or genuinely changed graphs still use fallback.

Attached backing now overlaps its bar-facing boundary by two source pixels so
fractional sampling cannot interpolate with transparent pixels immediately outside
the panel. Glyph placement and cleanup stay unchanged; floating panels keep their
gap. The active saved Black profile was float, which can still attach when covering
an original picture-intruding card. No configuration values were changed.

The new fractional-sampling GPU regression failed against the old shader, then
passed at both bars with rounded/square panels and a floating-panel control.
All 187 selected native/GPU tests passed, including expanded style/blur/border/
fill cache reuse in SDR, temporal RGB and native HDR, history policy, original and
stable surround, transparency, failure/retry and continued rendering. Final clean
x64 Release solution build completed successfully.

Same existing trial updated and restarted at
E:\codex\reconstruction-fill-options-trial-20261026. Host and both stable/beta
renderer DLL hashes match this Release build. Backup:
backups\before-subtitle-cycle-bar-20261026-122745. The first full-manifest copy
encountered the running Config editor's locked executable; the update was completed
with only the player/renderers, retaining the original first-attempt runtime backup.
The editor session stayed open; no editor code change was needed. All six CFG/STATE
files matched their pre-copy hashes and were rechecked after launch. Main deployment
unchanged. No integration merge or public release performed.

Live telemetry after restart: four sampled two-second windows each submitted 120
frames, output about 59.951 Hz, source about 23.976 Hz, worker_failed=0. This checks
running playback only. Windows computer-use helper initialization repeatedly failed
(sandbox setup/trusted Node exit), so actual keyboard cycling and visible seam
acceptance remain for the user. Evidence: assets/VP-0140/cycle-bar-fix-37946697-20261026/.
