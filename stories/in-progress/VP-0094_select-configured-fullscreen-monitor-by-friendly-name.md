# VP-0094: Select the configured fullscreen monitor by friendly name

## Status

In progress. Implementation branch: `codex/vp0094-fullscreen-monitor-name`, based on
the current `origin/v1.1.015-beta` integration branch.

Implementation commit `6b9d14c` completed a clean x64 Release build and all 582
unit tests. Deployment runtime verification remains pending: it requires explicit
approval to make the smallest active-configuration edit and replace the paired
`VideoProcessor.exe`/`vprenderer\VideoProcessorVPRenderer.dll` build artifacts.

2026-08-05 deployment test failed acceptance: despite matching deployed Release
hashes and `fullscreen_monitor_name: EPSON PJ`, the host remained on the LG/default
monitor and emitted no VP-0094 selection diagnostic. The prior config and paired
binaries were restored from the timestamped pre-VP0094 backups; implementation
debugging remains required.

Follow-up commit `1be83f2` fixed both causes: configuration now enters explicit
dialog state instead of a synthesized command line, and active paths use
`GetDisplayConfigBufferSizes` plus the documented `QueryDisplayConfig` retry.
The 2026-08-05 redeployment passed the x64 Release build and all 582 tests. Live
diagnostics resolved `DISPLAY1=LG HDR QHD`, `DISPLAY2=EPSON PJ`, selected the
Epson, verified the created host on the requested `HMONITOR`, and subsequent
display-timing diagnostics remained on `DISPLAY2`. Renderer/toggle/disconnect
acceptance checks remain pending before moving the story to review.

Startup testing then exposed a display-switch ordering issue specific to the
existing borderless fullscreen path: the original Epson host remained logically
visible, correctly placed, foreground, and focused, but the projector could
continue composing its desktop until fullscreen was toggled and a fresh host was
created. Follow-up commit `279cbf7` re-arms native fullscreen-host recovery after
`WM_DISPLAYCHANGE`, then re-shows and reapplies the configured monitor bounds and
z-order before focus. The x64 Release build and all 582 tests passed. The
2026-08-05 deployment logged the madVR display change followed by successful
placement recovery on `EPSON PJ`; physical startup confirmation and the remaining
acceptance checks are still pending.

## User story

As a VideoProcessor user with an Epson projector that is not always connected
or primary, I want VP to create its fullscreen renderer window on a configured
active monitor named `EPSON PJ`, so normal startup reliably uses the projector
without requiring the launch batch file to rearrange Windows displays.

## Context

VP currently derives the fullscreen host monitor with
`MonitorFromWindow(this->GetSafeHwnd(), MONITOR_DEFAULTTONEAREST)`. Therefore
fullscreen placement depends on wherever Windows initially placed the hidden
main dialog. The deployed launcher attempts to make the projector primary, but
`C:\Videoprocessor\mm\set_display_mode.ps1` currently identifies targets only
by maximum width (`3840`), not by the actual projector.

The active Epson is currently reported by MultiMonitorTool as:

- display path: `\\.\DISPLAY2`;
- monitor name: `EPSON PJ`;
- monitor ID prefix: `MONITOR\SECA805\`;
- serial: absent/blank.

The human-readable monitor name is the requested public configuration value.
The monitor-ID prefix is useful diagnostic evidence but is not required in the
first public configuration surface.

## Scope

1. Add an optional main configuration setting:

   ```ini
   [general]
   fullscreen_monitor_name: EPSON PJ
   ```

   The setting affects only fullscreen host placement. Existing `fullscreen`
   and `windowed_fullscreen_mode` settings retain their current meaning.
2. Before constructing or reconstructing the fullscreen host, enumerate active
   Windows monitors and map each `HMONITOR`/`\\.\DISPLAYn` source to its
   `DISPLAYCONFIG_TARGET_DEVICE_NAME.monitorFriendlyDeviceName` through
   `QueryDisplayConfig`/`DisplayConfigGetDeviceInfo`.
3. Match `fullscreen_monitor_name` as an exact, trimmed, case-insensitive
   friendly-name match. Do not use a partial name, resolution, monitor order,
   `DISPLAYn`, or primary-monitor status as a substitute.
4. When exactly one active monitor matches, pass that `HMONITOR` directly to
   `FullscreenVideoWindow::Create`/`CreateWindowedFullscreen`; do not rely on
   main-dialog placement or move the main UI just to select the target.
5. When the setting is omitted, the configured name is unavailable, monitor
   enumeration fails, or multiple active monitors share the name, preserve the
   existing `MonitorFromWindow(..., MONITOR_DEFAULTTONEAREST)` behavior. VP
   must still start and log the exact fallback reason.
6. Use this one shared selection path for Alpha and every DirectShow renderer,
   including renderer restarts, fullscreen toggles, display-mode changes, and
   host reconstruction. The VP OSD/renderer window must follow the newly
   selected host as they do today.
7. Add clear, rate-limited diagnostics containing requested friendly name,
   active monitor candidates (friendly name and `DISPLAYn`), selected monitor,
   and fallback/ambiguity reason. Never log or depend on display serial values.
8. Update the checked-in configuration and `CONFIGURATION.html` with one clear
   example and the disconnected-monitor fallback. Do not change the deployed
   user's configuration until explicit deployment approval.

## Explicit exclusions

- Do not make the Epson primary, enable/disable monitors, change resolutions,
  or invoke MultiMonitorTool from VP.
- Do not hard-code `\\.\DISPLAY2`, `SECA805`, a resolution, or a machine-specific
  monitor serial number.
- Do not force fullscreen when the user has set fullscreen off; this setting
  selects a target only when VP creates a fullscreen host.
- Do not alter alpha rendering, DirectShow graph lifecycle, refresh switching,
  color management, queue policy, or OSD content.
- Do not add a second public monitor-ID setting in this story. A future need
  for duplicate friendly names can justify a separate, explicitly documented
  advanced selector.

## Verification

1. With `fullscreen_monitor_name: EPSON PJ` and the projector active, launch
   VP in fullscreen with Alpha and with madVR. Confirm each fullscreen host and
   OSD appear on the projector even when the main dialog opens on another
   monitor and the projector is not primary.
2. Trigger renderer restart, fullscreen off/on, refresh switch, and renderer
   swap. Confirm each rebuilt fullscreen host remains on the configured Epson.
3. Disconnect/disable the Epson or use a display arrangement where it is not
   active. Confirm VP starts normally on its previous fallback monitor and logs
   `configured monitor unavailable`; no display arrangement is changed.
4. Test omitted setting, whitespace/case normalization, and two deliberately
   identical friendly names. Confirm omitted retains existing behavior and
   ambiguous values fall back rather than selecting arbitrarily.
5. Confirm windowed VP behavior is unchanged and the x64 Release build/tests
   succeed.

## Acceptance criteria

- `fullscreen_monitor_name: EPSON PJ` targets the one active Epson friendly
  name for all fullscreen renderer host creations.
- A disconnected, unavailable, ambiguous, or unsupported configured monitor
  never prevents VP from starting and never changes the Windows display layout.
- The fallback is deterministic, existing behavior is preserved, and the log
  explains selection or fallback without machine-specific hard-coding.
- Alpha and DirectShow fullscreen operation, OSD placement, input handling,
  renderer lifecycle, and windowed mode have no regression.
