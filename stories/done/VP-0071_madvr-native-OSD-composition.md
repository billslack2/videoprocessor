# VP-0071: Compose the VP OSD through madVR

## Status

Backlog. No implementation has started.

## User story

As a VideoProcessor user running madVR, I want VP's diagnostics OSD to be
composited by madVR itself, so it remains correctly layered over video in
windowed and fullscreen-exclusive presentation and does not depend on a
separate VP overlay window.

## Problem and goal

VP currently owns its madVR diagnostics OSD outside the renderer's presentation
composition. That creates avoidable window/Z-order/lifecycle complexity during
fullscreen transitions, renderer rebuilds, display changes, and exclusive
presentation. madVR exposes `IMadVROsdServices`, which supports application OSD
bitmaps and rendering callbacks in both windowed and exclusive mode.

Migrate the **madVR-only** VP diagnostics OSD to that API. The initial design
must use madVR-managed RGBA bitmap elements, retaining VP's existing text,
layout, dynamic height, visibility shortcut, and update cadence. This is not a
rewrite of the Alpha native/libplacebo OSD; VP-0044 remains its placement and
scaling story.

## Confirmed interface facts

The public `mvrInterfaces.h` shipped by MPC-HC/MPC-BE documents
`IMadVROsdServices` as allowing bitmap OSD elements and render callbacks, with
support for both windowed and exclusive mode. Bitmap elements are the safer
first integration because VP already produces a text panel; render callbacks
would couple VP drawing to madVR's render-target lifetime and state.

`IMadVRInfo` additionally provides `fullscreenRect`, `videoOutputRect`,
`croppedVideoOutputRect`, `subtitleTargetRect`, `exclusiveModeActive`, and
`osdLatency`. These are useful for placement and diagnostics, but they do not
provide queue occupancy.

## Required behavior

1. Query `IMadVROsdServices` only from the active madVR renderer instance.
   Absence of the interface, a failed call, renderer replacement, or device
   loss must leave playback functional and fall back safely to the existing VP
   OSD path for that renderer generation.
2. Register one named VP diagnostics panel as a 32-bit RGBA bitmap element.
   Update or hide that same element; do not create an unbounded element per
   OSD refresh.
3. Preserve all current content and controls: Ctrl+I visibility, dynamic panel
   height, display-rate warmup text, queue/scene/renderer diagnostics, and
   renderer-switch visibility handoff.
4. Compose the panel above madVR video in windowed and fullscreen-exclusive
   mode. It must not become a separate top-level window or require a VP window
   repaint to become visible.
5. Derive the initial placement from madVR's current output/fullscreen geometry
   and use the existing VP lower-right policy where applicable. Do not move the
   Alpha placement/scaling rules into madVR; keep renderer-specific geometry
   ownership explicit.
6. Treat `osdLatency` as diagnostic information only. VP must not block capture,
   delivery, reset, refresh switching, or UI work waiting for OSD composition.
7. On madVR stop, renderer switch, graph rebuild, display change, or destruction,
   hide/remove the element and release all interface/bitmap resources before
   their renderer generation is retired. Never submit an old-generation bitmap
   to a new madVR instance.
8. Preserve the existing Alpha libplacebo OSD unchanged. When Alpha is active,
   no madVR OSD interface or bitmap must remain active.

## Lifecycle and threading constraints

- Establish and tear down the interface with the same renderer-generation
  ownership used for madVR lifecycle transitions.
- Confirm COM apartment/thread affinity and API call safety before selecting the
  update thread. If the current OSD producer cannot safely call the interface,
  marshal/coalesce updates to the renderer-safe owner; do not add a blocking
  cross-thread call per frame.
- Coalesce updates: render and submit only when visible OSD content, geometry,
  or visibility changes. The normal live diagnostics refresh interval is
  acceptable; capture/delivery hot paths are not.
- A failed or delayed OSD submission is diagnostics-only and must be logged
  once per failure state, not per frame.

## Required investigation before implementation

1. Inspect the exact `mvrInterfaces.h` version used by the supported madVR
   installation and verify bitmap registration/update/removal semantics,
   coordinate spaces, alpha format, and any required bitmap flags.
2. Confirm which existing VP component owns madVR's `IUnknown` and renderer
   generation, and where the current OSD window is created, painted, hidden,
   and destroyed.
3. Test both normal windowed presentation and fullscreen-exclusive presentation
   with a minimal static bitmap. Verify device/display-mode and renderer-switch
   teardown before migrating dynamic text.
4. Decide and document the fallback behavior for an old/missing interface and
   for an individual API failure. The fallback must be observable in logs and
   must not duplicate two visible OSDs.

## Diagnostics

Log only lifecycle and state changes:

- madVR renderer generation, interface availability, selected OSD backend
  (`madvr-bitmap` or fallback), and reason for fallback;
- OSD element creation/update/hide/removal, bitmap dimensions, requested panel
  rectangle, relevant madVR geometry, exclusive state, and `osdLatency`;
- API failures with HRESULT, operation, and generation; and
- renderer switch/rebuild teardown completion.

Do not add per-frame logging or let an OSD failure trigger a renderer restart.

## Verification

1. With madVR selected, toggle the OSD repeatedly in normal windowed,
   maximized/borderless, and fullscreen-exclusive presentation. It must be
   visible only once, layered above video, and retain readable dynamic layout.
2. Exercise renderer switching (Alpha <-> madVR), madVR restart, VP restart,
   display refresh/resolution changes, HDR/SDR transitions, and graph rebuilds.
   Verify no stale OSD, duplicate panel, flash, crash, or retained COM resource.
3. Verify deliberate forced API/interface failure selects the logged fallback
   without interrupting capture, DirectShow delivery, or madVR rendering.
4. Confirm no measurable delivery, queue, dropped-frame, UI-liveness, or
   low-latency regression with diagnostics both shown and hidden.
5. Re-run the Alpha OSD tests relevant to VP-0044 and confirm its behavior is
   unchanged.

## Acceptance criteria

- The standard VP diagnostics OSD is composed through `IMadVROsdServices` when
  madVR exposes the supported interface.
- It works in both windowed and fullscreen-exclusive madVR presentation without
  a separate VP overlay window.
- Lifecycle transitions cannot leave a stale/duplicate OSD or call a retired
  madVR instance.
- Unsupported/failed integration degrades to one clearly logged safe fallback
  without affecting playback.
- Alpha OSD ownership, output, and placement behavior are unchanged.

## Dependencies and related work

- VP-0044: Alpha native OSD visible-picture anchoring and scaling (separate
  renderer and geometry implementation).
- VP-0060: madVR fullscreen transition target ownership.
- `include\\mvrInterfaces.h` from the supported madVR SDK/installation.
- `src\\VideoProcessor-GUI\\VideoProcessorDlg.cpp` and the current madVR
  renderer/OSD ownership code, to be identified during the required
  investigation.
