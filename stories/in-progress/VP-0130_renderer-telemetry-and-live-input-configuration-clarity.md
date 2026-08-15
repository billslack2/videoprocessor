# VP-0130: Renderer telemetry and live input-configuration clarity

## Status

In Progress (2026-08-15). Implementation is on
`codex/vp0130-renderer-ui-live-apply` in the clean worktree
`C:\Videoprocessor\vp\worktrees\vp0130-renderer-ui-live-apply`, based on
the current default branch `origin/v1.2.001-beta` tip
`e45b0aa0e5290051eb69f9413a493a49b8c40c7c`.

Implementation checkpoint:

- The Modern Renderer card now uses the available space more efficiently.
  Both backends show renderer uptime; DirectShow additionally reports its
  configured Start/Stop timestamp method, and VP Renderer reports its real
  successful present count. The key remains blue/left and the value
  white/right. Queue health stays in its separate card.
- The UI deliberately does not invent a backend-neutral `Late` count and does
  not add a hard-coded VP processing-path label. Current telemetry does not
  support either claim truthfully for both backends.
- Applying **Hide legacy renderers** rebuilds General, Actions, and Shortcuts
  from the editor's single renderer-discovery snapshot, preserves the active
  editor page, and retains a currently selected legacy renderer rather than
  silently changing the saved configuration.
- General Input processing continues to use the established renderer-restart
  live-apply transaction. The four General values are shared defaults only;
  explicit `[directshow]` and `[vprenderer.input_processing]` values remain
  backend-owned overrides and are never flattened on Apply.
- Clean x64 Release builds of the GUI, configuration editor, and relevant
  tests succeeded. The full configuration-editor suite and focused native
  apply-policy test passed.

## User story

As a VideoProcessor operator, I want the compact Modern Renderer card and the
configuration editor to show useful, trustworthy state and make applied
renderer/input policy clear, so I can diagnose the active backend without
confusing shared defaults with backend-specific overrides.

## Scope

1. Add compact, truthful renderer-card telemetry without combining it with
   queue health:
   - all renderers show uptime while rendering;
   - DirectShow shows the configured Start/Stop timestamp method;
   - VP Renderer shows only its existing successful-present counter;
   - labels are blue/left and values white/right in the existing metric-row
     treatment.
2. Use the spare visual space around Hardware link/HDR/Queue cards to retain a
   readable, compact Modern layout. Keep the renderer Restart action and all
   existing queue controls functional.
3. On successful Apply of `general.hide_legacy_renderers`, refresh every
   renderer-discovery-dependent editor surface (General selector, Actions, and
   renderer shortcuts) immediately from the same startup discovery snapshot.
   Preserve the selected page and any still-configured hidden renderer value;
   do not silently select another renderer.
4. Make the General Input processing description and effect summary explicit:
   `video_conversion`, `container_colorspace`, `hdr_colorspace`, and
   `hdr_luminance` are shared defaults and Apply requests the existing
   controlled renderer restart.
5. Preserve override/inheritance semantics. Applying a General default must
   not write, remove, copy, or replace a DirectShow or VP Renderer-specific
   override. The restarted backend resolves its own explicit value first and
   inherits only when that value is absent.

## Acceptance criteria

- The Modern Renderer tile uses the standard key/value colours and alignment;
  DirectShow shows uptime plus Start/Stop, VP Renderer shows uptime plus a
  real successful-present count, and no hard-coded VP processing-path or
  fabricated late-present metric appears.
- Queue information and Reset remain in the Queue health card rather than
  being duplicated in Renderer.
- Toggling **Hide legacy renderers** then clicking Apply updates the editor's
  renderer-dependent selections immediately, without reopening the editor or
  discarding the current page. Turning it off restores the discovered legacy
  choices.
- General Input processing changes display **Restart renderer** as their
  pending effect and signal the running VP only after a successful validated
  save.
- A configuration with distinct DirectShow and VP Renderer input overrides
  retains both byte-for-byte semantic values after an unrelated General Apply;
  no backend override is copied into `[general]` or into the other backend.
- x64 Release builds plus focused UI, persistence, and apply-policy tests
  pass. Live operator visual review remains required before release.

## Non-goals

- Defining a new generic `late present` renderer API or displaying an inferred
  value as an actual presentation metric.
- Adding a hard-coded VP processing-path text merely to fill the card.
- Moving queue diagnostics or configuration controls into the Modern operator
  Renderer card.
- Changing DirectShow/VP conversion algorithms, renderer discovery rules, or
  override precedence.

## Dependencies and references

- VP-0091: hide System32 DirectShow renderers by default.
- VP-0097: standalone configuration editor and safe persistence.
- VP-0102: dual-mode Modern operator UI.
- VP-0103: safe configuration application to a running VP.
- VP-0123: split renderer input policy and inheritance contract.
