# VP-0105: Toggle the runtime UI with a configurable shortcut

## Status

In Progress (2026-08-09). Reopened after live validation found that the
default `Ctrl+Shift+U` chord was also delivered to the foreground
configuration editor. The runtime command is dispatched, but the editor may
hide before the result is visible. The shortcut observer now consumes this
VP-owned chord after it posts the toggle command, with focused dispatch and
layout logs for further live diagnosis. Live validation also found that the
existing `Ctrl+Shift+S` Configuration Settings shortcut did not reliably hide
and reveal the separate configuration-editor window, so both VP-owned global
commands are now routed through the same observer.

## Implementation checkpoint (2026-08-09)

- Added the fixed configurable `[shortcuts] toggle_noui` action with
  `Ctrl+Shift+U` as its built-in default.
- Reused the existing no-UI layout and made it reversible by preserving the
  prior child visibility, minimum window size, and normal window placement.
- Kept capture, renderer, configured startup state, and fullscreen selection
  out of the transition; the command changes only the main-window layout.
- Supported revealing the Modern interface lazily after startup in `noui` and
  restored existing Modern or Classic controls on later toggles.
- Added the shortcut to both configuration editors, the sample configuration,
  the public-field inventory, and the canonical configuration reference.
- Added Qt editor coverage for the built-in default and configured round trip,
  plus the shortcut to shared editor-core round-trip coverage.

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
- When the configuration editor owns focus, the shortcut is handled by
  VideoProcessor and must not be delivered to the editor as an unrelated key
  chord or hide/close the editor.
- Toggling UI presentation must not restart capture or the renderer, change
  the configured/startup `noui` value on disk, or alter fullscreen state.
- Invalid or duplicate configured bindings follow the existing shortcut
  validation and logging behavior.
- Document the action in the sample configuration and configuration reference.

## Verification

- Complete x64 Release solution build passed on commit `caa0710` after rebasing
  onto the current `v1.2.001-beta` default.
- Qt configuration-editor tests: 15 of 15 passed, including the new default
  and configured-shortcut round trip.
- Native tests: 723 of 728 passed. The five failures are the established
  configuration/reference baseline failures:
  `ConfigurationReferenceMatchesPublicFieldInventory`,
  `Vp0097NamedViewportsUseFileOrderAndIgnoreLabels`,
  `Vp0079OwnerVariantsResolveWithoutPersistedProfileState`,
  `ConfigEditorCoreRoundTripsEveryEditorOwnedKey`, and
  `ConfigEditorCoreValidatesEveryEditableOrderedProfileSurface`.
- Remaining reviewer validation: confirm the shortcut hides and restores the
  controls during active video from normal, startup `noui`, and fullscreen
  ownership without interrupting presentation.

## Acceptance criteria

- One shortcut toggles normal controls off and back on during the same process.
- Active video continues through both transitions without a renderer restart.
- Startup `/noui` or `noui=true` can use the shortcut to reveal the controls,
  and the next press returns to video-only presentation.
- The shortcut is configurable and documented with a safe default.
- The x64 Release build and focused regression tests pass.

## Integration target

- `v1.2.001-beta` (repository default branch discovered 2026-08-09).
