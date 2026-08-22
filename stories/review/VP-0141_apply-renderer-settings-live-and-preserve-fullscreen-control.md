# VP-0141: Apply VP Renderer settings live and preserve fullscreen control

## Status

Review. Implementation is committed as `892668f` on
`codex/vp-0141-live-settings`, based on the current 2026-08-22
`origin/v1.2.001-beta` integration tip `802038d`. The Qt/libplacebo review
confirmed the existing immutable render-thread safe point and cache lifecycle
are preserved.

The incident and source review establish a bounded implementation path:
compatible libplacebo processing changes must use the existing render-thread
live-update path, while fullscreen and Config input, z-order, and close
routing must remain recoverable during an arbitrarily slow render call.

The effective-delta gate now
queues compatible immutable settings for the existing libplacebo render-thread
safe point; fullscreen raw-key forwarding and asynchronous Config target
acknowledgement are implemented. The Config acknowledgement uses an explicit,
validated VP endpoint so a mutable fullscreen owner cannot receive or lose the
reply. Validation: x64 Release builds of Lib, VPRenderer, GUI, Config, and
ConfigTests; the full Config suite; the focused native ownership/async-ack
test; and the native suite (861 passed).

Deployment completed 2026-08-22 from x64 Release commit `892668f` after a
clean 57-file manifest stage. The host, VP Renderer plugin, Config executable,
and Config discovery DLL were backed up and replaced as one validated runtime
set; deployed SHA-256 hashes match staging. `VideoProcessor.cfg` and
`vprenderer/VideoProcessorShaderCache.bin` were preserved without edits.

Remaining review/acceptance: manually change each of the five incident
settings while `rec709_scope_med` is active in fullscreen, verify the log says
`compatible live update queued`, and confirm Config, configured shortcuts,
Alt+F4, and normal exit remain responsive during any cold shader compile.

## Problem

Applying changes to the active VP Renderer profile while fullscreen caused a
complete renderer reconstruction, a 5.1-second cold libplacebo pipeline
compile, and an unrecoverable operator state. Video eventually rendered, but
the topmost fullscreen surface covered the UI and VP no longer responded to
configured keyboard commands or close attempts. Windows logout was required
to terminate it.

The saved configuration changed only rendering parameters inside the already
selected `rec709_scope_med` profile:

- `upscaler`: `AUTO` to `ewa_lanczos4sharpest`;
- `downscaler`: `AUTO` to `ewa_lanczos`;
- `deband_strength`: `AUTO` to `default`;
- `sigmoid`: `AUTO` to `on`; and
- `dithering`: `AUTO` to `on`.

No viewport, output transport, D3D device, swapchain, cache policy, or
fullscreen setting changed. Nevertheless, VP logged `renderer rebuild
required` and the generic claim `Unified viewport output aspect changed`.

## Evidence and diagnosis

The 2026-08-22 15:24 live log records:

1. Config classified the edit as `Apply rendering live`, with category
   `vprenderer.rec709_scope_med`.
2. The selected display/profile names remained unchanged and the viewport
   remained `scope`, aspect `47:20`, with `changed=0`.
3. VP reconstructed the renderer and preserved the fullscreen host.
4. The persistent libplacebo cache loaded 29 objects / 184,024 bytes, but the
   new EWA scaling pipeline was not present.
5. First render spent approximately 5.11 seconds in GLSL-to-SPIR-V,
   SPIR-V-to-HLSL, and HLSL-to-DXBC compilation and reported
   `render_ms=5149.28`, `shader=none`. This was the base libplacebo conversion,
   scaling, deband, sigmoid, and dithering pipeline, not an NLS hook.
6. Config remained the foreground process while the preserved fullscreen
   `WS_EX_TOPMOST` popup covered it. The asynchronous presentation-target
   `PostMessage` was logged as `ack=0 accepted=1`; enqueue success was not an
   application-level acknowledgement.
7. VP suppresses global VP shortcuts while Config owns foreground, and the raw
   fullscreen HWND does not explicitly route all configured accelerators to
   the main dialog. Rendering telemetry continued, but there was no close
   routing or orderly shutdown before logout terminated the process.

`LibplaceboVideoRenderer::ApplyApplicationState` currently identifies a
rendering-profile change by comparing selected profile names. Editing values
inside the same selected profile therefore bypasses the compatible live path
and falls through to the default `profile_update_mode=rebuild` fingerprint
policy. VP already has a compatibility gate and a coalesced render-thread
application path that preserve the renderer, device, swapchain, and program
cache.

Libplacebo render parameters are dynamic. Changing a scaler or pass topology
may still require a genuine first-use program compilation, but it does not
require replacing the libplacebo renderer or its presentation host.

## Objective

Apply compatible VP Renderer processing edits through the existing
render-thread live-update boundary, and guarantee that Config, fullscreen
commands, and application shutdown remain responsive and visually reachable
while libplacebo performs a cold compile of any duration.

## Requirements

1. Base the live/rebuild decision on the effective settings delta and the
   existing compatibility boundary, not solely on whether a selected profile
   name changed.
2. Treat scaling, quality, tone/gamut mapping, peak detection, contrast
   recovery, debanding, sigmoid, dithering, display bit depth, SDR processing,
   and compatible LUT/render-description changes as live candidates when the
   existing compatibility check accepts them.
3. Reconstruct only for an actual device, swapchain, output-transport,
   presenter, or shader-cache-policy incompatibility. Preserve the current
   strict rejection behavior if a purported rendering-only update crosses
   that boundary.
4. Queue live settings as immutable intent and apply them only at the existing
   render-thread safe point. Do not call libplacebo concurrently, create a
   second compilation context, precompile a variant matrix, or add VP-owned
   shader-cache lifecycle management.
5. A cold `pl_render_image` may delay the new video pipeline, but must never
   block the MFC or Qt event loops, wait on the UI thread, destroy/recreate the
   fullscreen host, or hide the last successfully presented frame merely to
   show compilation state.
6. If compilation status is enabled, derive it from the actual slow render
   call, show a small non-activating/input-transparent `Preparing video
   pipeline...` indicator, and keep it out of window ownership and focus
   policy. Warm calls remain silent.
7. Give the Config-to-presentation-target change a real asynchronous,
   sequence-aware acknowledgement. Do not report `PostMessage` enqueue success
   as receiver acceptance and do not introduce a synchronous cross-process
   wait.
8. While Config is visible and owns foreground, keep it visibly above the
   fullscreen host with `SWP_NOACTIVATE`. Defer and retry the z-order reassert
   while a Qt-owned popup is active so combo boxes and menus are not disrupted.
9. Route Alt+F4 and all configured presentation shortcuts explicitly from the
   raw fullscreen HWND to the main command owner. Exit and fullscreen escape
   must not depend on MFC discovering a non-MFC HWND through dialog
   `PreTranslateMessage`, Config foreground ownership, or the global shortcut
   hook.
10. Renderer teardown and any render-thread join must remain asynchronous from
    both UI threads. A hung Config process or a blocked GPU render must not
    prevent the application close path from being requested and visibly
    acknowledged.
11. Replace the generic `viewport output aspect changed` restart message with
    the actual classified reason and effective changed fields. Log live-update
    queueing/application, genuine rebuild boundaries, ownership acknowledgement,
    z-order repair, fullscreen key forwarding, and close routing without
    per-frame noise.
12. Preserve standard libplacebo cache ownership. A graceful renderer
    destruction may persist newly compiled objects; forced logout must not be
    compensated for by a new VP cache manager.

## Acceptance criteria

1. Editing the five incident settings inside the currently selected profile
   leaves the renderer generation, fullscreen HWND, D3D device, swapchain, and
   in-memory libplacebo cache intact.
2. The edit is queued and applied on the render thread, and the log reports a
   compatible live update rather than a viewport or renderer reconstruction.
3. A deterministic test blocks the render worker for at least five seconds
   while both MFC and Qt message-loop heartbeats remain responsive.
4. During that blocked render, the operator can interact with visible Config,
   close Config, leave fullscreen, select another renderer, and request VP
   shutdown without waiting for the compile.
5. With fullscreen and visible Config, Apply never leaves the fullscreen host
   visually above a foreground Config window after the acknowledged target
   transition. An open Qt combo/menu remains usable and z-order repair occurs
   safely after it closes.
6. With focus placed directly on the raw fullscreen HWND, every configured
   accelerator reaches the common command dispatcher and Alt+F4 posts the
   application close request within 100 ms, including while rendering is
   blocked.
7. A dead or hung Config process cannot block VP's UI thread or make
   fullscreen inescapable; target acknowledgement times out asynchronously and
   leaves a safe command/close route.
8. An intentionally incompatible output/device/cache-policy edit still takes
   the documented rebuild path and records the exact boundary that required
   it.
9. A cold scaling pipeline may compile once, then renders warm from the same
   in-memory cache. A normal graceful shutdown persists the cache through
   libplacebo's existing lifecycle without new prewarming or cache merging.
10. No-shader and NLS/NLS+ modes retain their current rendering, geometry, and
    cache behavior except for the corrected live-apply and UI-safety contract.
11. Focused lifecycle/input tests, the complete native suite, the complete
    Config UI suite, and a clean x64 Release build pass before deployment.
12. Live validation covers windowed and fullscreen Apply, a deliberately cold
    high-quality scaler pipeline, visible Config with a popup, renderer
    switching, fullscreen escape, Alt+F4, graceful cache save, and restart.

## Boundaries and related work

- VP-0140 remains complete: VP does not precompile or manage synthetic shader
  variants. This story uses standard on-demand libplacebo compilation.
- VP-0103 owns the broader saved-configuration live-apply contract. This story
  fixes VP Renderer's same-profile effective-delta classification and the
  fullscreen failure exposed by that workflow.
- VP-0111 owns configurable foreground/background shortcut policy. This story
  does not broaden background shortcuts; it guarantees routing inside VP's own
  active fullscreen surface and an unconditional close escape.
- VP-0134 owns general renderer handoff and display-state restoration. This
  story preserves the existing fullscreen host for compatible live changes
  and addresses Config/fullscreen ownership acknowledgement at that boundary.
- Do not add a preparation process, background libplacebo context, pipeline
  epoch system, speculative shader enumeration, or custom persistent-cache
  format.
- Do not make the UI wait for confirmation from libplacebo, the GPU driver, or
  the separate Config process.

## Likely implementation areas

- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- `src/VideoProcessor-GUI/VideoProcessorDlg.cpp`
- `src/VideoProcessor-GUI/FullscreenVideoWindow.cpp`
- `src/VideoProcessor-GUI/VideoProcessorApp.cpp`
- `src/VideoProcessor-Config/ConfigEditorWindow.cpp`
- focused VP Renderer lifecycle, fullscreen-input, and Config integration tests
