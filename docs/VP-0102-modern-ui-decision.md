# VP-0102 Modern UI architecture and toolkit decision

## Decision

Use native MFC/Win32 for the Modern operator window. Keep the existing
`CVideoProcessorDlg` Classic presentation available and behaviorally unchanged.
Extract only the narrow runtime observation and operational-command seams needed
for both presentations; do not move capture, renderer, color, queue, timing, or
fullscreen policy into the Modern view.

Qt 6 Widgets was considered because the VP-0097 configuration editor establishes
a Qt deployment path. It is not selected for the live operator window. The live
VP process already owns a native MFC message loop, accelerator handling, tray and
fullscreen behavior, renderer child-window hosting, display-topology recovery,
and shutdown ordering. Adding a Qt top-level window would introduce a second
presentation lifecycle and a cross-toolkit native-child boundary without
reducing the required extraction from the current dialog.

## Interface selection and isolation

- `/noui` creates neither Classic nor Modern controls and has highest precedence.
- A valid process `/interface classic|modern` overrides persisted configuration
  for that process only.
- A valid persisted `[general] interface` selection applies otherwise.
- Missing or invalid configuration resolves to Modern.
- Invalid or incomplete `/interface` logs one actionable warning and falls back
  through persisted configuration to Modern; it does not fail capture startup.
- Modern is the effective default. Classic remains available through persisted
  configuration or `/interface classic`.
- Classic and Modern have independent placement keys and minimum-size rules.

The interface is resolved before constructing either concrete presentation.
The selected view consumes the same runtime coordinator, video output host, and
command paths. Rollback consists of selecting Classic or removing the Modern
construction branch; no capture or renderer policy depends on Modern.

## Approved normal geometry

At 100% DPI the approved design canvas is 1680x716 client pixels:

- application header: 56 px high;
- workspace margins: 16 px horizontal, 14 px top, 16 px bottom;
- fixed telemetry column: 512 px;
- inter-column gap: 16 px;
- renderer/video host: 1120x630 px, exactly 16:9.

The telemetry column and its controls do not stretch. When the operator resizes
the window, remaining width belongs to the video host and the host retains 16:9.
The resize implementation must preserve the usable work area and cannot assume
that the monitor begins at desktop coordinate (0,0).

Compact/minimum and enlarged DPI-aware metrics remain design-review gates before
the Modern view is considered complete. They must keep every mandatory item
visible without reducing text below the approved accessible typography.

## Video host

Reuse the existing native renderer/output host and presentation pipeline. Modern
changes only the parent layout. It must not create another renderer, capture path,
queue, compositor, conversion path, or timing policy. Non-16:9 source content
continues to use the renderer's normal aspect-preserving behavior within the
16:9 host.

## Ownership, threading, and messages

- The application retains one native UI thread and one MFC message pump.
- Runtime state remains owned by the existing capture/renderer coordinator while
  presentation-neutral snapshots are copied to the active view.
- Restart capture, restart renderer, reset queues, and open configuration dispatch
  to shared command handlers; Modern never reimplements lifecycle or save logic.
- Existing accelerators, fullscreen focus routing, display-change handling, and
  orderly shutdown remain on the native thread.

## Modern title bar

The approved visual design uses a Modern-only custom non-client caption. It is
feasible, but visual similarity is not sufficient for acceptance. The caption
must preserve:

- `WS_THICKFRAME`, `WS_SYSMENU`, minimize, maximize, and close semantics;
- drag and resize hit testing, double-click maximize, and Alt+Space;
- Windows 11 Snap Layouts by returning `HTMAXBUTTON` over the maximize target;
- correct work-area bounds when maximized;
- per-monitor DPI changes and `WM_DPICHANGED` suggested bounds;
- keyboard focus, UI Automation names, and High Contrast colors;
- DWM activation, shadow, and dark-mode behavior where supported.

The Config action is a fixed, keyboard-focusable client control visually aligned
with the application identity. It launches the existing standalone configuration
editor and is not itself a configuration surface.

If the custom-caption feasibility matrix fails on a supported Windows/DPI/High
Contrast combination, Modern falls back to the native DWM caption plus a compact
client header. Classic always retains its current native caption.

## DPI policy and review gates

Use per-monitor DPI-aware Win32/MFC sizing. Store logical placement independently
for Classic and Modern, scale from the active monitor DPI, clamp to that monitor's
work area, and reject undersized restored geometry.

Before acceptance, verify 100%, 150%, and 200% DPI; High Contrast; keyboard-only
operation; Snap Layouts; multiple monitor origins; normal/maximized/fullscreen
transitions; both renderers; and clean shutdown. The custom caption receives a
focused regression pass separate from the visual review.
