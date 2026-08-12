# VP-0120: Retire owner-bound configuration tray processes

## Status

Backlog (2026-08-11). Development testing left four live
`VideoProcessorConfig.exe` processes and four notification-area icons after
their corresponding VP test processes had exited. They were real resident
processes from separate test directories, not stale Explorer icons.

## User story

As a VideoProcessor developer and operator, I want a background configuration
editor started by VP to retire with its owning VP process when it is safe, so
sequential test builds do not leave a row of indistinguishable tray icons while
standalone or unsaved configuration work remains protected.

## Current evidence and cause

- Config intentionally calls `QApplication::setQuitOnLastWindowClosed(false)`
  and closing its window normally hides it in the tray.
- Config's single-instance event is scoped by installation directory. This is
  correct for side-by-side builds, but it permits one resident Config process
  for every temporary test folder.
- `StartConfigurationEditorInTray()` currently launches Config with
  `--background` but without the owner HWND/PID used by the visible launch
  path. Config therefore cannot associate its lifetime with the VP process
  that warmed it.
- The observed processes came from VP-0117, VP-0118, and VP-0119 test folders;
  no `VideoProcessor.exe` process remained alive.

## Scope

1. Pass a validated owner HWND and process ID when VP warm-starts Config in the
   background, using the same ownership identity as an explicit Config reveal.
2. Let an already-running same-install Config accept or refresh that ownership
   through a non-revealing association message. Background warm-up must never
   steal focus or display the editor.
3. Monitor the validated owner process with a waitable process handle rather
   than polling window visibility. Handle normal exit, crash, and VP window
   recreation without confusing HWND lifetime with process lifetime.
4. When the owner exits, automatically hide/remove the tray icon and terminate
   Config if it is hidden and has no unsaved changes.
5. Preserve a visible or dirty editor after owner exit. Detach its stale native
   owner safely; after changes are saved/discarded and the operator closes it,
   exit instead of returning an ownerless VP-launched editor to the tray.
6. Preserve intentional standalone Config behavior: a directly launched
   editor with no VP owner may remain tray-resident until **Exit** is chosen.
7. Keep instance identity installation-scoped. Multiple genuinely running VP
   installations may each own one Config process; never route one build to
   another build's executable or configuration file.
8. Make remaining side-by-side icons diagnosable by including a concise build
   or installation identity in the tooltip/menu without exposing an unwieldy
   full path.

## Safety constraints

- Do not enumerate and terminate arbitrary `VideoProcessorConfig.exe`
  processes from a new build.
- Do not issue guessed `NIM_DELETE` calls for notification icons owned by
  another process/window.
- Do not replace the installation-scoped singleton with one global Config
  singleton; that could open or edit the wrong build's configuration.
- Never discard unsaved edits merely because VP exited or crashed.

## Acceptance criteria

- Launching and closing four clean VP test builds sequentially leaves no
  owner-bound Config processes or tray icons after all VP processes exit,
  provided each Config editor is hidden and clean.
- While one VP installation is running, repeated warm starts and Config
  shortcut/settings-button activations reuse exactly one same-install editor
  and one tray icon.
- A background association does not reveal Config, activate it, move focus, or
  interrupt an open combo box.
- A hidden clean editor exits promptly after normal or abnormal owner-process
  termination and removes its tray icon through its own normal shutdown path.
- A visible or dirty editor survives owner exit with its edits intact, clearly
  reports that VP is no longer running, and exits rather than hiding after the
  operator resolves the edits and closes it.
- A standalone Config launch remains usable and tray-resident independently of
  VP, and **Exit** always removes its icon.
- Two simultaneously running side-by-side VP installations retain two isolated,
  identifiable Config processes, each editing only its own configuration.
- Automated lifecycle tests cover owner validation, non-revealing reassignment,
  clean hidden exit, dirty/visible preservation, standalone behavior, and
  same-install single-instance reuse; a clean x64 Release build passes.

## Non-goals

- Cleaning up tray processes created by already-deployed older binaries.
- Managing madVR's notification-area lifetime.
- Preventing users from intentionally running multiple VP installations.

