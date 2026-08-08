# VP-0097 main-window configuration inventory

This inventory treats the existing VideoProcessor window as two different surfaces:

- **Persistent policy** belongs in `VideoProcessor.cfg` and may be edited by the configuration application.
- **Live state and commands** belong in the running VP window. They must not be represented as saved settings unless VP first defines a durable configuration contract for them.

## Main-window controls

| VP main-window item | Kind | Configuration contract | Qt editor status |
|---|---|---|---|
| Capture device | Persistent startup choice | `[general] capture_device` | Editable discovered selector; blank means not configured and an unavailable configured name is retained |
| Capture input connection (`HDMI`, `SDI`, etc.) | Persistent startup choice when a device supports multiple connector types | `[general] capture_input` | Editable discovered selector; a single-choice Quad HDMI channel simply reports HDMI |
| Capture Restart | Live command | None | Never saved |
| Renderer | Persistent startup choice | `[general] renderer` | Editable discovered selector; blank means not configured and an unavailable configured name is retained |
| Renderer Restart | Live command | None | Never saved |
| Container colorspace | Shared persistent input policy | `[general] container_colorspace` | Editable finite selector on General; legacy `[directshow]` value is migrated when edited |
| HDR colorspace policy | Shared persistent input policy | `[general] hdr_colorspace` | Editable finite selector on General; legacy `[directshow]` value is migrated when edited |
| HDR luminance policy | Shared persistent input policy | `[general] hdr_luminance` | Editable finite selector on General; legacy `[directshow]` value is migrated when edited |
| Queue Use | Live enable switch | None | Not exposed; needs a config contract before addition |
| Queue capacity | Persistent queue profile value | `[queue]` or `[queue.<name>]` `queue_size` | Editable in the Qt ordered Queue profile page |
| Queue Reset | Live command | None | Never saved |
| Queue Auto | Live policy switch | No Boolean contract | Not exposed. Recovery thresholds are persistent queue settings, but this checkbox itself is not |
| Scene Detect | Persistent startup policy | `[general] scene_detect` | Editable on General |
| Video conversion | Shared persistent input conversion | `[general] video_conversion` | Editable finite selector on General; applies to VP Renderer and DirectShow |
| Start/Stop method | Persistent DirectShow policy | `[directshow] renderer_start_stop_time_method` | Editable finite selector |
| DirectShow nominal range | Persistent DirectShow override | `[directshow] renderer_nominal_range` | Editable finite selector |
| DirectShow transfer function | Persistent DirectShow override | `[directshow] renderer_transfer_function` | Editable finite selector |
| DirectShow transfer matrix | Persistent DirectShow override | `[directshow] renderer_transfer_matrix` | Editable finite selector |
| DirectShow primaries | Persistent DirectShow override | `[directshow] renderer_primaries` | Editable finite selector |
| Frame offset / Auto | Persistent DirectShow timing policy | `[directshow] frame_offset` (`AUTO` or non-negative milliseconds) | Editable selector with custom numeric entry |
| Fullscreen mode | Live control with persistent initial state | `[general] windowed_fullscreen_mode` seeds startup only | Editable on Startup; changes made in the VP main window are not written back |
| Fullscreen | Live control with persistent initial state | `[general] fullscreen` seeds startup only | Editable on Startup; changes made in the VP main window are not written back |
| HDR metadata values (MaxCLL, MaxFALL, mastering minimum/maximum) | Live manual override when HDR luminance is User | None | Not exposed; changing them in VP is session-only |

## Read-only runtime information

These values describe the current device, signal, renderer, or queue. They are deliberately **not configuration fields**:

- capture and renderer state text;
- device “Other properties”, including PCIe link speed and width;
- input lock, video-frame count, misses, and capture hardware latency;
- captured-video validity, display mode, pixel format, EOTF, and detected colorspace;
- detected HDR chromaticity coordinates and white point;
- current MaxCLL, MaxFALL, and mastering luminance outside the live User override mode;
- the CIE diagram;
- queue current occupancy and dropped-frame count;
- VP timing, PTS lead, and time-to-PTS.

LLDV profiles can intentionally author metadata policy, but that does not turn the main window's current/detected HDR readout into editable configuration.

## Configuration already represented outside the main window

- **General:** capture and renderer selection, fullscreen behavior, monitor targeting, start minimized, scene detection, and new LLDV detection. Legacy-renderer filtering and scene-correction/detection override settings remain manual-only and are preserved by the editor. `fullscreen_monitor_session_mode` remains parser/runtime compatibility only and is deliberately not exposed while that feature is broken.
- **Queue profiles:** queue depth, lead frames, startup preroll, steady target, active-picture lookahead, renderer-restart delay, and queue high-water reset threshold. Every field is profile-selectable; ordered profiles support shortcuts and rules.
- **VP Renderer profiles:** output, color, tone/gamut mapping, scaling, debanding, dithering, and related VP Renderer policy. Ordered profiles support shortcuts and rules.
- **Viewports:** geometry, subtitle behavior, ordered fallback/conditional selection, shortcuts, and rules.
- **LLDV:** the editor intentionally exposes only the first configured metadata policy, without profile, shortcut, or rule controls. Runtime compatibility still accepts later conditional sections, but the editor does not advertise or author them.
- **Shaders:** shipped NLS modes, required stage/backend files, shortcuts/rules, and arbitrary shader-specific key/value parameters. NLS mode precedence follows its visible list/file order; numeric `order` remains manual-only for custom `type: multi` stacks where effects may intentionally compose. The internal `shader_type` discriminator is preserved but not shown as a disabled form control.
- **Actions:** ordered action definitions with event, command, condition, and renderer scope (`*`, VP Renderer, or a discovered one-based renderer index).
- **Shortcuts:** global application shortcuts.

## Remaining configuration-editor work

1. Decide whether Queue Use and Queue Auto deserve persistent contracts. Recovery thresholds are now fully profile-selectable, but the two main-window switches are still live controls.
2. Improve labels for the two fullscreen startup Booleans if we want them presented as one mutually understandable mode selector. Their underlying configuration is already supported.

Per the product requirement, the editor does not expose `[directshow.conversion]` or `[directshow.ppm]` at all. It preserves those sections unchanged during edits. Runtime telemetry should only be added later through an explicit VP status API, not by scraping the VP window or logs.
