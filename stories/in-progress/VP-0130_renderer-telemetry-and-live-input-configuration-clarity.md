# VP-0130: Renderer telemetry and live input-configuration clarity

## Status

In Progress (2026-08-15). Implementation is on
`codex/vp0130-renderer-ui-live-apply` in the clean worktree
`C:\Videoprocessor\vp\worktrees\vp0130-renderer-ui-live-apply`, based on
the current default branch `origin/v1.2.001-beta` tip
`e45b0aa0e5290051eb69f9413a493a49b8c40c7c`.

Implementation checkpoint:

- Implemented and pushed `3a9d652` (`feat(ui): clarify renderer telemetry
  and live apply`) on `codex/vp0130-renderer-ui-live-apply`.
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
  tests succeeded. The full configuration-editor suite, both focused editor
  tests, and the focused native apply-policy test passed.
- Deployed on 2026-08-15 from `3a9d652` after a fresh x64 Release build:
  `VideoProcessor.exe`, `vprenderer\\VideoProcessorVPRenderer.dll`, and
  `config\\VideoProcessorConfig.exe`. The previous binaries are backed up at
  `C:\\Videoprocessor\\vp\\backups\\vp0130-3a9d652-20260815-113950`.
  SHA-256 verification passed for all three deployed artifacts. The tracker
  now awaits code and live operator visual review.
- Operator review produced follow-up `7497335` (`fix(ui): refine renderer card
  diagnostics`). Renderer identity is now a blue/left `Renderer` row with a
  white/right value, leading `DirectShow - ` is removed for display, Restart
  is restored to the bottom of the enlarged Renderer card, Hardware link is
  shorter, and Queue health uses non-overlapping, aligned metric columns.
  PCIe speed below Gen 2 and width below x4 now receive warning glyphs; the
  permanent undocumented CLI-only `/always_warn_pci` switch forces both for
  visual testing.
- The corrected x64 Release host/plugin pair was deployed and SHA-256 verified
  on 2026-08-15. Its previous binaries are backed up at
  `C:\\Videoprocessor\\vp\\backups\\vp0130-7497335-20260815-115418`.
- Follow-up operator review produced and deployed `c8b372c` (`fix(ui): restore
  queue metric alignment`). Queue labels and values again share the original
  left edge and spacing, warning glyphs expose the hover text **May impact
  performance**, the DirectShow row reads `Start/Stop`, and VP Renderer leaves
  the backend-specific third row empty. The x64 Release host/plugin hashes
  matched after deployment; the previous pair is backed up at
  `C:\\Videoprocessor\\vp\\backups\\vp0130-c8b372c-20260815-120242`.
- Final layout and recovery follow-up produced and deployed `6248fe9`
  (`fix(ui): prevent clipped queue metrics and global bare shortcuts`). Queue
  values now have four additional logical pixels of bottom-border clearance.
  The
  hang was traced to the background shortcut observer dispatching the bare
  `R` reset accelerator while another application owned focus; ordinary text
  entry generated reset requests until DirectShow request 11 wedged and
  shutdown waited behind it. Background observation now requires at least one
  Ctrl, Alt, or Shift modifier, while bare shortcuts continue to work when VP
  owns focus. The focused native policy test and a clean x64 Release solution
  build passed. The orphaned process was force-stopped, the host/plugin pair
  was SHA-256 verified after deployment, and the replacement launched with a
  responsive main window. Backup:
  `C:\\Videoprocessor\\vp\\backups\\vp0130-6248fe9-20260815-121529`.
- Configuration-editor responsiveness follow-up produced and deployed
  `8ecfc5f` (`fix(config): keep cached selectors responsive`). Capture and
  renderer discovery remain cached for the editor process, monitor discovery
  now refreshes only when Qt reports a real screen-topology change, and the
  active-profile timer pauses while hidden or while a selector popup owns
  focus. Unchanged profile state no longer emits redundant model updates. The
  full configuration-editor test suite, including real dropdown interaction
  and the new no-invalidation regression, passed. The previous editor is
  backed up at
  `C:\\Videoprocessor\\vp\\backups\\vp0130-8ecfc5f-20260815-122759`.
- Cold-start testing exposed an inconsistent parser: the valid shared setting
  `video_conversion: NONE` was translated to `/video_conversion NONE`, but
  the startup parser rejected it even though validation and live Apply accept
  it. Follow-up `0809d04` (`fix(config): accept disabled video conversion at
  startup`) centralizes parsing and accepts `NONE`/`OFF` plus the existing
  conversion spellings. The focused x64 Release regression test passed, the
  clean x64 Release host/plugin pair built successfully, and source/deployed
  SHA-256 hashes matched. The deployed application launched with the existing
  configuration and a responsive main window. Backup:
  `C:\\Videoprocessor\\vp\\backups\\vp0130-0809d04-20260815-123500`.
- Deeper independent review of the remaining selector delay produced and
  deployed `64ca44d` (`fix(config): make discovery selectors respond
  immediately`). The reported Capture device, Monitor, and Renderer controls
  are now true non-editable selectors, so clicking the field opens the popup
  instead of focusing an embedded text editor. Ordinary edits no longer run
  disk-backed candidate validation synchronously on Qt's UI thread; complete
  validation is retained for Apply/OK, while discovery-name checks remain
  immediate. Warm Config reveal now grants the editor foreground permission
  before signaling it, preventing the first click from being consumed solely
  to activate a previously hidden editor. New regressions click the center of
  all three selectors and require the popup within 250 ms, prove ordinary
  edits remain responsive when the validation temp path is unavailable, and
  enforce the foreground-grant ordering. The full configuration-editor suite
  and clean x64 Release host, renderer, and Config builds passed. All three
  deployed artifacts matched their source SHA-256 hashes; the deployed host
  and Config window launched responsive. Backup:
  `C:\\Videoprocessor\\vp\\backups\\vp0130-64ca44d-20260815-130000`.
- Legacy renderer visibility follow-up `4ee7cb4` (`fix(config): filter legacy
  renderers immediately`) makes the checkbox a UI-only live filter. Toggling
  it immediately rebuilds General, Actions, and renderer Shortcuts from the
  cached discovery snapshot, preserves the active page and pending document,
  and performs no rediscovery. Saving the preference no longer requests a
  renderer restart. The focused regression and complete Config editor suite
  passed, clean x64 Release host and Config builds completed, and both
  deployed hashes matched. Backup:
  `C:\\Videoprocessor\\vp\\backups\\vp0130-4ee7cb4-20260815-131000`.
- Operator testing showed `4ee7cb4` still spent 5–8 seconds reconstructing the
  complete 12-page editor shell on each toggle. Performance correction
  `1f074cb` (`perf(config): filter cached renderers in place`) now constructs
  General, Actions, and renderer Shortcut rows once from the all-renderer
  snapshot and only changes existing row visibility. No control or page is
  recreated. The deterministic regression includes useful and built-in legacy
  renderers, verifies all three surfaces, requires the same widget instances,
  and enforces a sub-250 ms toggle. The focused regression and complete Config
  suite passed; the clean x64 Release Config executable was SHA-256 verified
  after deployment. Backup:
  `C:\\Videoprocessor\\vp\\backups\\vp0130-1f074cb-20260815-132500`.

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
   - VP Renderer leaves the backend-specific third metric row empty;
   - labels are blue/left and values white/right in the existing metric-row
     treatment.
2. Use the spare visual space around Hardware link/HDR/Queue cards to retain a
   readable, compact Modern layout. Keep the renderer Restart action and all
   existing queue controls functional.
3. Treat `general.hide_legacy_renderers` as an immediate UI-only filter. On
   toggle, refresh every renderer-discovery-dependent editor surface (General
   selector, Actions, and renderer shortcuts) from the same startup discovery
   snapshot without waiting for Apply or performing discovery again. Preserve
   the selected page and any still-configured hidden renderer value; do not
   silently select another renderer or request a renderer restart.
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
  DirectShow shows uptime plus Start/Stop, VP Renderer shows uptime with its
  backend-specific third row empty, and no hard-coded VP processing-path or
  fabricated presentation metric appears.
- Queue information and Reset remain in the Queue health card rather than
  being duplicated in Renderer.
- Toggling **Hide legacy renderers** updates General, Actions, and renderer
  Shortcuts immediately, without Apply, rediscovery, a renderer restart,
  reopening the editor, or discarding the current page. Turning it off restores
  the already-discovered legacy choices.
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
