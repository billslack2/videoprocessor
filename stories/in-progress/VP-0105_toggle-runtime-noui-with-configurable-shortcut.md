# VP-0105: Toggle the runtime UI with a configurable shortcut

## Status

In Progress (2026-08-09). Story accepted for immediate implementation. The
source branch is `codex/vp-0105-toggle-noui`, based on the repository default
branch `v1.2.001-beta` discovered from GitHub on 2026-08-09.

## User story

As a VideoProcessor operator, I want a keyboard shortcut that toggles between
the normal controls and `noui` presentation while video is running, so I can
hide or restore the operator UI without restarting VideoProcessor or changing
its launch arguments.

## Requirements

- Add a fixed configurable action in `[shortcuts]` for toggling the runtime UI.
- Give the action a documented built-in default that does not conflict with
  existing built-in shortcuts.
- When the controls are visible, the shortcut switches to the same windowed
  video-only presentation used by startup `noui`.
- When already in runtime `noui` presentation, the same shortcut restores the
  normal controls and usable prior window placement.
- The shortcut works from the normal dialog, the video panel, and fullscreen
  ownership paths covered by the existing shortcut dispatcher.
- Toggling UI presentation must not restart capture or the renderer, change
  the configured/startup `noui` value on disk, or alter fullscreen state.
- Invalid or duplicate configured bindings follow the existing shortcut
  validation and logging behavior.
- Document the action in the sample configuration and configuration reference.

## Verification

- Add focused coverage for default/configured shortcut registration and the
  reversible visibility transition where practical.
- Run the relevant automated tests and a complete x64 Release solution build.
- Confirm manually that the shortcut hides and restores the controls without
  interrupting active video.

## Acceptance criteria

- One shortcut toggles normal controls off and back on during the same process.
- Active video continues through both transitions without a renderer restart.
- Startup `/noui` or `noui=true` can use the shortcut to reveal the controls,
  and the next press returns to video-only presentation.
- The shortcut is configurable and documented with a safe default.
- The x64 Release build and focused regression tests pass.

## Integration target

- `v1.2.001-beta` (repository default branch discovered 2026-08-09).
