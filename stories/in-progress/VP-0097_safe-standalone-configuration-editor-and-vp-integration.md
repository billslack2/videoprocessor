# VP-0097: Safe standalone configuration editor and VP integration

## Status

In Progress. On 2026-08-06 the developer confirmed the freshly discovered
`billslack2/videoprocessor` default branch `v1.1.016-beta` as the implementation
base. Implementation is on `codex/vp-0097-config-editor` in
`C:\Users\bslac\vp\vp-0097-config-editor`, based on
`origin/v1.1.016-beta` at `b6e28923dc0ceb2f4786e048bb8119fb0dd46b42`.

The first increment is intentionally limited to a runnable standalone editor:
read-only configuration preview, notification-area behavior, and a small set
of safe structured edits. VP launch/shortcut integration will follow once the
editor executable and its configuration safety boundary are validated.

2026-08-06 Qt 6 Widgets migration started (uncommitted VP source work):

- Approved Qt 6 Widgets as the replacement for the coordinate-driven Win32
  presentation layer. The migration preserves the established VP header,
  persistent left navigation, sticky action footer, and side-by-side ordered
  profile list/detail structure while handing layout, scrolling, repainting,
  DPI behavior, focus, and standard controls to Qt.
- Added a parallel `VideoProcessorConfigQt.exe` x64 target rather than
  replacing the working editor prematurely. Its first runnable shell includes
  Startup, Queue, VP Renderer, Viewports, LLDV, and Shortcuts navigation; Qt
  system-tray behavior; direct `--config` launch; `--owner` integration; the
  embedded VP icon; independent page scrolling; and packaged Qt runtime DLLs.
- Extracted `ConfigEditorCore`, a UI-independent lossless document and safe
  persistence layer. It retains comments, unknown/manual content, original
  line endings and terminal newline, validates the complete candidate through
  VP schemas, creates a timestamped backup, and atomically replaces the file.
  Three focused preservation/safe-save tests pass.
- Startup and global Shortcuts are the first editable Qt surfaces and already
  use the shared safe-save core. Ordered Queue, Renderer, Viewport, and LLDV
  pages use the approved Qt profile layout and display real profile names,
  order, shortcut, and rule data, but their detail editing remains read-only
  for this migration slice. The Win32 editor remains the production fallback
  until those pages, tray/activation integration, and regression coverage
  reach parity.
- Qt 6.8.3 is pinned as a repository-local ignored dependency with a
  reproducible bootstrap script. The dedicated x64 Release Qt target builds
  and `windeployqt` packages successfully. The complete solution could not
  finish while the currently running VP and Qt executables locked their output
  files; this is an environment lock, not a Qt compile/link failure.
- Independent UI review approved the Qt shell as the continuing design basis
  with no P0 blocker. Follow-up fixes made the profile preview explicitly
  read-only, removed false reorder instructions, corrected viewport display
  labels/default status, distinguished read-only fields and disabled primary
  actions, and removed the unnecessary Startup scrollbar at the default size.

2026-08-06 first increment implemented (uncommitted source work):

- Added the standalone `VideoProcessorConfig.exe` Win32 project with a
  notification-area icon, single-instance activation, direct `--config` launch,
  optional VP owner-window handle, config summary, Validate/Reload/Save actions,
  and a deliberately narrow structured surface for General, Alpha output, and
  queue size.
- The editor works from a line-preserving document, so its structured save
  changes only an existing selected value and leaves comments, profiles,
  actions, shaders, unknown content, ordering, and assignment spelling alone.
  It validates candidate text through `ConfigFile`, `MainConfigSchema`, and
  `RendererProfileConfig`, then makes a timestamped backup and atomically
  replaces the file.
- Added the configurable `[shortcuts] config_editor` VP command (Ctrl+S by
  default). VP launches the editor beside its executable with the resolved
  primary config path and its main dialog as owner.
- x64 Release solution build succeeded. The focused `ConfigFileTests` run
  passed 41/41. The editor startup smoke test passed against the checked-in
  config. A visible instance is running against the disposable build-output
  config for developer feedback; it does not use the deployed configuration.

2026-08-06 polish follow-up (uncommitted source work):

- Replaced the generic application/tray image with the embedded VideoProcessor
  icon. The editor now uses the VP icon in its title bar, task switcher, and
  notification-area icon.
- Refined the initial visual hierarchy with a VP-branded heading, quieter
  background, clearer category labels, more breathing room, and an explicit
  Advanced/read-only boundary.
- Closing the editor now visibly confirms that it remains in the notification
  area; it uses the current notification-area protocol and the tray menu still
  provides Open and Exit. The rebuilt x64 Release solution completed
  successfully. The visible feedback instance was restarted from that build.

2026-08-06 configuration-surface refinement (uncommitted source work):

- Flattened the editor navigation into four direct pages: **Startup**,
  **Queue**, **VP Renderer**, and **Viewports**. Queue and renderer controls
  are now deliberately separate rather than hidden together under a vague
  General page.
- The Queue page presents queue depth, lead frames, and target frames. The VP
  Renderer page presents output selectors and an optional advanced rendering
  section; controls are not present on the wrong page when navigating or when
  advanced rendering is expanded.
- A UI-quality review caught and corrected the advanced-section visibility
  regression introduced by the split. The x64 Release build succeeded and the
  focused ConfigFileTests suite passed 43/43. A fresh disposable-config editor
  instance was started for feedback; no deployed configuration was changed.

2026-08-06 profile-model correction (next editor increment):

- Viewports are not the only conditional configuration domain. The current
  VP-0079 runtime treats `[queue]` / `[queue.<name>]` and `[vprenderer]` /
  `[vprenderer.<name>]` as baseline-plus-named-variant groups using the same
  `when:` expression model. The editor must expose their baseline and named
  variants rather than editing only the root sections.
- The Queue and VP Renderer pages will therefore use the same operator-facing
  pattern as Viewports: a selected profile list, readable name, condition
  field with expression help, add/remove/reorder lifecycle, and structured
  fields for the selected profile. A named-only group follows file order for
  its fallback; an exact root remains its explicit baseline. Existing manual
  keys are preserved when a structured field is changed.
- `[general]` / Startup remains a single application-wide page. Shader groups
  also use conditional variants, but full shader editing remains manual for
  this first structured surface; their contents must continue to round-trip
  untouched.

2026-08-06 tray regression correction (uncommitted source work):

- Feedback exposed that the icon was absent after closing. The cause was a
  Win32 initialization-order bug: the notification-area registration occurred
  during `WM_CREATE`, before the editor stored its own window handle. The
  handle is now assigned during `WM_NCCREATE`, before tray registration.
- A fresh x64 Release solution build including the corrected launchable
  `x64\\Release\\VideoProcessorConfig.exe` completed successfully. The old
  disposable feedback process was stopped so the next direct launch uses the
  corrected build.

2026-08-06 tray interaction refinement (uncommitted source work):

- The tray interaction now follows the normal Windows convention: left-click,
  double-click, and keyboard selection open the editor; right-click opens a
  menu containing **Open configuration** and **Exit**. The tooltip documents
  those actions and the menu dismisses cleanly after selection.
- The standalone x64 Release project build passed. The currently running
  feedback executable must exit before the directly launchable Release copy
  can be refreshed, because Windows locks an executing `.exe`.

2026-08-06 notification callback correction (uncommitted source work):

- User testing found that the visible tray icon still ignored every click. The
  first implementation compared the entire version-4 notification payload to
  the mouse event; Windows stores the event in its low word and the icon ID in
  its high word. The handler now reads the event correctly and supports click,
  double-click, right-click/context-menu, and keyboard notifications.
- The locked disposable feedback process was stopped, the solution-targeted
  x64 Release build passed, and the corrected editor was restarted from
  `x64\\Release` for immediate testing.

2026-08-06 viewport and presentation increment (uncommitted source work):

- Reworked the prototype layout into a VP-branded header and grouped General,
  Output & queue, and Viewport configurations areas. The help text now states
  the actual safety boundary rather than presenting a placeholder/advanced
  sidebar.
- Added selector-driven support for the canonical `[vprenderer.viewport]` root
  and every `[vprenderer.viewport.<name>]` variant in file order. The selected
  viewport exposes supported aspect, anamorphic, crop, subtitle-fit, timing,
  and padding controls; its activation rule remains visible but manual.
- The editor updates only changed viewport settings and can add a selected
  known setting to an existing viewport section. It preserves unedited manual
  rules, shaders, profiles, comments, and unknown settings. Standalone x64
  Release build passed; focused `ConfigFileTests` passed 41/41.

2026-08-06 user-feedback correction (uncommitted source work):

- Fixed the first presentation pass issues revealed by live feedback: an
  encoding-dependent header glyph, clipped overlong help text, a Win32
  accelerator marker in the Output & queue caption, and known-value combo
  controls that did not select their loaded value.
- The viewport selector now always puts the root base viewport first and
  explicitly labels it **Default (base viewport)**. When no root exists, the
  parser-compatible first named viewport is labeled as the default; remaining
  named viewport variants are listed in configuration-file order.

2026-08-06 viewport-management follow-up (uncommitted source work):

- Corrected the test assumption after inspecting the active deployment
  configuration: it has the base, `scope`, and `scope_anamorphic` viewport
  variants. The previous UI showed one only because it was launched against
  the checked-in sample, which has only the root section.
- Added New viewport and Remove selected actions, protected the required base
  viewport, and exposed the activation rule as a validated advanced field.
  New variants start with safe viewport defaults and remain pending until
  saved; removal requires in-app confirmation and saving retains a backup.
- Changed aspect, anamorphic, subtitle timing, and padding values to
  selector-first controls. A configured nonstandard value remains selectable
  and unchanged rather than being forced to a listed standard value.
- Reduced the initial UI type scale and launched the x64 Release editor
  against a disposable copy of the active configuration for safe multi-
  viewport feedback. The active deployed config was not modified.

2026-08-06 validation and placement correction (uncommitted source work):

- Live testing found that loading queue size used a combo-box selection message
  on an Edit control, blanking the value and causing validation to reject an
  otherwise valid configuration. The edit control now receives its text
  directly; this does not alter the test or deployed configuration.
- The editor now clamps its opening position to the monitor work area so its
  branded header is not created partly above the visible desktop.

2026-08-06 scalable UI redesign (uncommitted source work):

- Replaced the single growing settings canvas with category navigation and
  separate **General & output** and **Viewports** pages. The page/control
  grouping is designed to accept additional renderer, queue, shortcut,
  shader-selection, and advanced categories without extending one long form.
- Replaced the viewport combo with a persistent default-first viewport list
  and a separate detail editor. Named viewport add/remove actions live with
  that list; the selected viewport's condition and settings remain on the
  detail side.
- Corrected the viewport control semantics: aspect ratio, anamorphic scale,
  subtitle timing, padding, and activation condition are text/numeric inputs;
  only genuinely finite contracts remain dropdowns. Inline explanations and
  examples now explain screen aspect, anamorphic correction, subtitle fields,
  naming rules, and the formerly unexplained `when` expression as an
  **Activation condition**.
- Reflowed the viewport detail form after visual feedback: full-width
  checkbox labels no longer collide with a second column, subtitle inputs use
  one readable vertical sequence, the activation explanation is explicitly
  two lines, and footer actions retain usable widths at the initial window
  size.
- Reduced the window to a compact work-area-safe size while retaining smaller
  typography and minimum resize bounds. The x64 Release target completed, the
  redesigned process stayed running against the disposable three-viewport
  configuration, and focused `ConfigFileTests` passed 41/41.

2026-08-06 UI-foundation gate (uncommitted source design work):

- New visual feedback correctly identified that the prototype is still an
  unthemed, coordinate-driven raw Win32 form, not a foundation for more
  configuration pages. Its current screenshot also exposes selector-height
  clipping and label/control reflow defects, so no further configuration
  surface will be added until the shell is rebuilt.
- A UI designer and Windows UI engineering review produced the source design
  contract at `docs/VP-0097_CONFIGURATION_EDITOR_UI_DESIGN.md`: VP theme
  tokens, typography, navigation, page/card/form/footer primitives, field
  control policy, viewport interaction design, DPI/accessibility requirements,
  and a review gate for the first two pages.
- The selected default direction retains the existing native C++ config and
  integration behavior while replacing direct child-control coordinates with
  a measured, DPI-aware native presentation layer. WinUI 3/C++/WinRT remains
  an explicit alternative only if its deployment/runtime decision is approved
  before implementation.

2026-08-06 native UI-foundation implementation (uncommitted source work):

- Rebuilt the editor shell using a Per-Monitor-V2 manifest/runtime, scaled
  fonts, a measured content host with vertical scrolling, custom painted
  surfaces, sidebar navigation, VP-colored action buttons, a sticky footer,
  and a compact modal Add viewport dialog. The General and Viewports pages no
  longer use group boxes or independently positioned field labels.
- Applied user visual feedback: short ratio/number inputs use compact fields
  with explicit units; condition input has a reasonable maximum width; the
  viewport list displays configuration names and neutral type text rather
  than `$key` expressions; and constrained width stacks cards vertically
  instead of clipping or horizontally shifting them. The stacked list fits
  three configured viewport rows, and the complete viewport detail card
  scrolls without clipping its final Padding field.
- Fixed configuration-editor safety behavior exposed by UI review: Validate
  and failed Save now retain pending text and selection. Save remains blocked
  until the existing VP schemas validate the candidate configuration.
- Default handling now follows the runtime format exactly: an explicit
  `[vprenderer.viewport]` root is the base/default when it exists; otherwise
  the first named viewport is default and the editor can safely make another
  named viewport first. Root-backed promotion is disabled rather than
  performing an unsafe header swap that would change `when` semantics.
- The UI quality reviewer completed a source-based layout sign-off after the
  final release rebuild: no horizontal overflow at the responsive breakpoint,
  card/detail bounds are complete, and the current three-viewport feedback
  configuration is fully visible. Standalone x64 Release build and focused
  `ConfigFileTests` passed 41/41. The feedback editor continues to use a
  disposable copy of the active configuration; nothing was deployed.

2026-08-06 ordered viewport identity increment (uncommitted source work):

- Defined the forward viewport format: every new viewport has a stable named
  section ID and an optional human-facing `label:`. The first named section in
  file order is the default/fallback; later named sections are rules. Labels
  support spaces and are accepted as UI-only metadata by startup validation;
  they never affect runtime selection, inherited settings, or action
  variables.
- The editor derives IDs without making the label lossy: spaces become `_`, a
  literal underscore becomes `__`, and duplicate generated IDs receive a
  numeric suffix. Existing named sections without a label display a readable
  form of their ID.
- A legacy `[vprenderer.viewport]` root remains fully readable and retains
  historical root precedence. The new **Name legacy** action is the only
  migration path: it renames the section, adds its label, preserves comments
  and manual settings, and places it first so it stays the fallback. A legacy
  `when` is explicitly explained as becoming the new default's direct
  selection rule rather than silently treated as an equivalent root reset.
- The checked-in sample now demonstrates the named-first format. The public
  field reference documents `label` and the identifier convention. New parser
  tests cover file-order default selection, label isolation, legacy-root
  precedence even when it appears later in the file, and rejection of the
  reserved `default` ID. The direct-launch path now uses the checked-in sample
  only for recognized local development builds; installed and VP-launched
  editors still use the installed or explicitly supplied configuration path.
- Standalone x64 Release and the rebuilt focused `ConfigFileTests` suite passed
  43/43. No deployment and no active-configuration edit occurred.

2026-08-06 direct-launch correction (uncommitted source work):

- Live direct-launch testing found the development executable trying to load a
  nonexistent `VideoProcessor.cfg` beside its build output. The fallback now
  recognizes only the local project layout, resolves the checked-in sample to
  a canonical path, and never applies that parent-directory fallback to an
  installed editor. Explicit VP `--config` paths remain authoritative.
- Validation diagnostics now identify the actual named viewport section and
  received value instead of reporting every named-baseline error as the legacy
  root section. The latest standalone x64 Release build and focused suite
  passed 43/43; the editor was reopened from that build for feedback.

2026-08-06 ordered viewport and expanded safe-controls increment (uncommitted source work):

- Replaced the one-off **Make default** action with drag-to-reorder for named
  viewports. The editor moves whole source sections, retaining their comments,
  rules, manual settings, and relative content. The first named viewport in
  literal file order remains the fallback. A legacy root remains deliberately
  non-reorderable until the user uses the explicit, explained **Name legacy**
  migration action.
- Changed anamorphic correction from an always-present ratio override to an
  explicit **Enable anamorphic stretch** checkbox plus a ratio field. Leaving
  it off removes the setting rather than inventing a false value; the disabled
  field explains that the unconfigured default is `1:1`.
- Expanded the General & output surface with the safe, production-schema
  bounded controls: start minimized; quality, presentation, range, gamma,
  SDR primaries, tone/gamut mapping, peak detection, up/downscalers,
  debanding, dithering; and queue depth/lead/target frame counts. Finite
  contracts use selectors; ratio and bounded numeric inputs remain text/numeric
  fields. Device discovery, HDR calibration, shaders, actions, and arbitrary
  rules remain manual/advanced rather than receiving unsafe free-form UI.
- The standalone x64 Release build and focused `ConfigFileTests` suite passed
  43/43. No deployment or active-configuration edit occurred.
- A follow-up UI review kept the new surface progressive: basic presentation
  and queue controls remain visible, while color/scaling controls live behind
  a reversible **Show advanced rendering** action. All selectors now use the
  normal compact field height. Reordering adds Ctrl+Up/Ctrl+Down, Escape
  cancellation, and captured edge autoscroll alongside drag-and-drop.

2026-08-06 selector popup correction (uncommitted source work):

- Live feedback found that compact raw Win32 dropdown-list controls could be
  focused and scrolled but did not present a usable popup. The editor now
  keeps the visible selector at the normal compact field height while
  explicitly configuring native item height, non-integral list sizing, and a
  minimum eight-item dropdown. DPI changes reapply those metrics. The Release
  editor rebuilt successfully and focused `ConfigFileTests` remained 43/43.

2026-08-06 native dropdown and strict UI-review correction (uncommitted source work):

- Screenshot feedback proved the first popup correction still produced only
  a one-pixel horizontal list. Raw `CBS_DROPDOWNLIST` controls require their
  full hidden popup height in the control rectangle; the face remains compact
  automatically. Layout now supplies a 240-DIP popup height, retains measured
  26-DIP rows and a wider popup, and no longer uses non-integral sizing.
- Startup and Output cards now size independently when Advanced rendering is
  expanded, eliminating the large blank Startup column. Owner-drawn controls
  gained hover states and system-color high-contrast fallbacks.
- Validation now routes only bare `[general]`, `[vprenderer]`, and `[queue]`
  errors to their corresponding structured controls. Named/manual rule errors
  remain document-level. Routed fields are focused, scrolled into view, and
  visibly outlined across edits, selectors, and checkboxes.
- The x64 Release editor rebuilt successfully and focused `ConfigFileTests`
  remained 43/43. No deployment or active configuration edit occurred.

2026-08-06 recorded resize and repaint correction (uncommitted source work):

- Frame-by-frame review of a fresh OBS recording confirmed real Win32 repaint
  damage rather than a recording artifact: expanding Advanced rendering and
  live-resizing left repeated combo faces, card-border trails, and ghosted
  footer buttons from intermediate nested-child layouts.
- Relayout now freezes painting until child rectangles are final, invalidates
  the complete hierarchy once, and uses top-level Win32 composition so the
  finished control tree is presented as one buffered frame. Card painting no
  longer excludes the intentionally tall hidden popup areas used by native
  dropdown-list controls.
- The responsive column decision is stable across scrollbar state, and the
  vertical scrollbar gutter remains reserved (disabled when unused). Enabling
  Advanced therefore changes vertical content without stealing client width
  or shifting both cards horizontally.
- UI quality review found no P0 issue and its two P1 findings—breakpoint
  oscillation and duplicate synchronous full-tree redraws—were corrected.
  The standalone x64 Release build succeeded and focused `ConfigFileTests`
  remained 43/43. The feedback process still edits only the worktree config;
  nothing was deployed.

2026-08-06 recorded scrolling and responsive-state correction (uncommitted source work):

- A second OBS review showed that the shared General-page scroll moved the
  short Startup card offscreen while scrolling the tall Advanced Output card.
  Startup is now sticky in the two-column layout; stacked layouts retain one
  natural document flow and reset to the top when crossing the responsive
  breakpoint so Startup cannot disappear during reflow.
- Scrollbar dragging now reads the native 32-bit track position and presents
  each tracked composed frame immediately, avoiding coarse delayed jumps.
  Enabling anamorphic stretch also repaints its ratio field immediately so its
  disabled gray value becomes visibly active black text.
- UI quality review found no P0/P1 issue in the sticky coordinates, transition
  reset, thumb-tracking path, or targeted anamorphic repaint. The standalone
  x64 Release build succeeded and focused `ConfigFileTests` remained 43/43.

2026-08-06 copy, state, and viewport-order controls (uncommitted source work):

- Removed the configuration-path banner, renamed the navigation/page from the
  raw-looking General/output spelling to **General**, and standardized normal
  control text to black with muted gray disabled states. The action blue was
  darkened after accessibility review so selected and hovered small text meets
  normal-text contrast.
- Added visible **Move up** and **Move down** viewport controls with correct
  legacy/first/last disabled states. Drag ordering and Ctrl+Up/Ctrl+Down remain
  available, and all paths preserve pending viewport edits before moving whole
  source sections.
- The next audited structured surfaces are **Shortcuts**, followed by
  **Queues & recovery**. Shortcut capture requires shared grammar/conflict
  validation before it is considered safe. Queue recovery remains active and
  must include its reset delay and high-water percentage rather than being
  treated as obsolete.
- The standalone x64 Release build succeeded and focused `ConfigFileTests`
  remained 43/43. UI quality review found no layout/reordering P0/P1 after the
  contrast correction.

2026-08-06 conditional profile and shortcut increment (uncommitted source work):

- Queue and VP Renderer now use the same file-ordered profile list/detail
  pattern as Viewports, including add, remove, rename, and reorder controls.
  Root sections remain explicit baselines; when no root exists, the first
  named profile is the default. Omitted named-profile settings retain their
  inherited state instead of being rewritten.
- Added a separate public `shortcut:` field beside the optional `when:` field
  for queue, renderer, viewport, and shader root/member sections. The raw file
  keeps both keys separate. Runtime canonicalizes the key chord and internally
  evaluates `(<when>) || ${key}=="<shortcut>"`; the editor never has to parse or
  rewrite a key clause inside the user's rule. Full shader editing remains
  manual and is preserved.
- Shortcut-first profile forms now hide the advanced rule editor behind a
  **Use automatic activation rule** checkbox. Existing `when:` content opens
  expanded automatically, and the rule field provides three editable lines.
  Viewport configuration entries are consistently presented as **Profiles**;
  their literal file order identifies the default.
- UI review findings were addressed with measured wrapping heights, wider
  responsive breakpoints that stack narrow detail cards, grayscale font
  antialiasing compatible with the composed Win32 window, and shorter copy.
  Runtime review also corrected command-ID collisions between legacy and
  unified profile shortcuts, queue override rollback, and viewport
  release-drift-only updates. Shortcut aliases/case are canonicalized before
  collision and selection handling.
- The complete x64 Release solution build succeeded and the focused
  `ConfigFileTests` suite passed 44/44. No deployment or active configuration
  edit occurred.

2026-08-06 ordered-rule and consistent-profile correction (uncommitted source work):

- Removed the Priority editor. The root profile is the default when present;
  otherwise the first named profile is the default. VP checks later profiles
  in file order and stops at the first matching rule. Existing handwritten
  `priority:` values remain readable and preserved but are deprecated and
  ignored, with file order now the single precedence model.
- A default/root shortcut now manually selects the default profile rather than
  briefly returning to automatic selection. Named shortcuts remain manual
  per-group selectors. Added a regression covering ordered overlapping rules,
  ignored legacy priority, and explicit default-profile shortcut selection.
- Queue, VP Renderer, and Viewports now share the same profile interaction:
  side-by-side sticky profile list and selected-profile detail at the same
  breakpoint; Add/Rename/Remove/Move controls all live on the list side. Only
  the detail side moves during vertical scrolling. Advanced renderer settings
  start expanded.
- Renamed the optional condition toggle to **Use rule**, removed the empty
  one-line rule box and obsolete Priority row, and explicitly applied the UI
  font to native combo popup list windows. The x64 Release solution succeeded
  and focused `ConfigFileTests` passed 45/45.
- A schema audit found no missing modern viewport controls. Next safe coverage
  should add Startup device/renderer/monitor selectors, Queue lookahead and
  still-active recovery settings, then renderer luminance/transfer/contrast
  controls and root refresh switching. Runtime-enumerated Startup values need
  typed schema choices before the editor exposes them.

2026-08-06 uniformly ordered and named profile correction (uncommitted source work):

- Removed the root-profile exception from the editor's working model. On load,
  legacy unnamed Queue, VP Renderer, and Viewport roots are migrated in memory
  to a unique readable name such as **Profile 1**, moved ahead of existing
  named siblings, and marked as an unsaved change. Existing viewport labels,
  settings, comments, and ordering are preserved. Save persists the migration
  through the existing timestamped-backup path.
- File order is now the only default indicator in all three profile editors:
  any profile can move into or out of the first/default position. Name is the
  first field in the selected profile's right-hand detail form; the separate
  Rename action is no longer part of the list controls.
- Editor validation now requires every profile name to be non-empty and unique
  case-insensitively. Named viewport sections missing a label receive a unique
  generated label during load, duplicate viewport labels are rejected at Add,
  and a validation error navigates to the offending page and row.
- Typography now follows the current Windows message font with regular body
  text and semibold hierarchy, including native combo popup lists. The final
  reviewer pass also corrected stale list text after inline renames and
  preserved existing unique legacy labels during automatic migration.
- Removed the renderer's advanced-settings disclosure entirely. All supported
  renderer values are always visible in one continuous profile form, with no
  mode-dependent resize or hidden state.
- The complete x64 Release solution build succeeded and focused
  `ConfigFileTests` passed 45/45. The editor was relaunched from the feature
  worktree against its checked-in configuration; no deployment or active
  configuration edit occurred.

2026-08-06 LLDV, shortcuts, and Queue recovery increment (uncommitted source work):

- Added dedicated **LLDV metadata** and **Shortcuts** pages. LLDV exposes the
  four optional public luminance overrides with numeric inputs and omission as
  VP default. Shortcuts exposes active fixed VP commands and checks their chord
  syntax and collisions with profile/shader bindings. Legacy renderer-only
  commands remain hidden.
- PPM and `[directshow.conversion]` are explicit permanent exclusions from the
  structured editor. Their source text and comments continue to round-trip
  untouched; no general DirectShow or unrestricted K/V page will expose them.
- Moved recovery keys canonically into the first/default Queue profile. Shared
  startup resolution uses `[queue]` when present or the first direct
  `[queue.<name>]` otherwise. Legacy `[queue_recovery]` remains readable, the
  editor migrates its active values, ambiguous duplicate definitions fail,
  and non-default profiles reject recovery keys. Reordering Queue profiles
  transfers recovery settings to the new default so safeguards do not change.
- Action renderer targeting will use a one-based renderer-list index selected
  from the UI, with `vprenderer` as the default and `*` for all renderers.
  Numeric targets are now accepted directly; aliases remain compatibility-only
  and will not be required or shown by the editor.
- The complete x64 Release solution build succeeded and focused
  `ConfigFileTests` passed 46/46. The updated editor was relaunched from the
  feature worktree; no deployment or active configuration edit occurred.

2026-08-06 ordered LLDV and expanded Startup increment (uncommitted source work):

- Converted LLDV from a singleton editor into the same ordered profile model
  used by Queue, VP Renderer, and Viewports. The first named profile is the
  default; later profiles support Shortcut and Rule, inherit omitted metadata,
  can be named/reordered/removed, and use shared runtime defaults without
  persisting a field merely because its effective default is displayed. The
  legacy exact `[lldv]` baseline remains readable and is migrated in the
  editor's working copy like other unnamed roots.
- Added production runtime selection for `[lldv.<name>]`, LLDV shortcut
  registration and `profile.lldv.changed` state, mode-aware resolved metadata,
  and safe live application with renderer restart only when the active
  renderer rejects the update. Updated the sample, public inventory, and HTML
  reference with the ordered LLDV contract.
- Expanded Startup to cover capture device, renderer, fullscreen monitor and
  session mode, start minimized, scene/detection controls, and HDR colorspace
  and luminance policy. It now uses three focused cards and friendly selector
  labels while preserving the exact stored tokens and any configured value
  that is not currently discoverable.
- Added shared read-only discovery for DeckLink capture devices, installed
  renderers, and active monitor friendly names. VP and the editor can reuse the
  same renderer ordering; toggling Hide legacy renderers refreshes the list
  without discarding the selected configured value. This keeps the editor a
  separate executable without duplicating device/runtime enumeration logic.
- Corrected Startup omission semantics after follow-up live feedback: omitted
  `capture_device` and `renderer` display the first discovered list item as
  their effective default, but remain absent from the file unless the operator
  chooses a different value. An explicit configured value always wins, even
  when currently unavailable. Omitted `hide_legacy_renderers` displays checked
  because the production default is true, while an explicit false remains
  respected.
- Exposed the existing global `newlldv` policy as **Use new LLDV detection
  heuristic** on Startup's Source analysis and HDR card; the `new_lldv`
  compatibility spelling still loads correctly. Runtime review determined
  that safe per-LLDV-profile support requires a separate raw-capture lookup:
  evaluating selection from synthesized PQ state can make a BT.2020/SDR rule
  select and immediately deselect itself. The future precedence contract is
  explicit process CLI, selected LLDV profile, legacy global setting, then
  false; profile-specific exposure is deferred until that runtime refactor.
- Made side-by-side list/detail presentation an explicit invariant for every
  ordered profile page—not only Viewports—and added it to the UI review gate.
  Queue, VP Renderer, LLDV, and Viewports now use the same normal-width
  breakpoint. Queue recovery and LLDV rows reflow inside a narrow detail pane
  without changing to a stacked profile page or clipping long labels.
- A final independent UI review found and fixed stacked-page clipping,
  premature profile-column breakpoints, long-list-label buffer overflow,
  long detail-heading clipping, and stale enabled fields for empty profile
  groups. The reviewer reported no remaining P0/P1 blocker in the scoped UI.
- The complete x64 Release solution build succeeded. Focused
  `ConfigFileTests` passed 48/48, including ordered LLDV selection/default
  inheritance and public-reference coverage. The build emitted only existing
  incremental-link PDB type-record warnings. No deployment or active
  configuration edit occurred. Actions and shader-selection pages remain the
  next structured UI increments; numeric/all-renderer action targeting is
  already accepted by the runtime and documented.

## User story

As a VideoProcessor operator, I want a selector-driven Windows configuration
application that can run independently or open from VP, so I can safely manage
the supported `VideoProcessor.cfg` settings and shortcuts without hand-editing
most values or losing advanced configuration that remains manual.

## Product decision

The editor is a separate Windows executable with a notification-area icon. It
edits the one normal public configuration file, `VideoProcessor.cfg`; it is not
another settings store and does not introduce a sidecar renderer configuration.

The first version provides structured controls for known public values and
safe, lossless preservation for configuration outside its structured editing
surface. Full shader-source editing remains manual. Arbitrary profile/action
expressions may initially use a validated advanced text field rather than a
complete visual rule builder.

## Current configuration review

The current default branch already has several useful foundations:

- application and renderer settings normally share one `VideoProcessor.cfg`;
- both `key: value` and compatibility `key=value` assignments parse;
- startup performs aggregate ownership validation plus the main and renderer
  schema passes;
- the checked-in sample has a focused test proving that it parses without
  warnings and passes both startup schemas; and
- `CONFIGURATION.html`, `docs/configuration-public-fields.tsv`, and
  `docs/configuration-public-values.tsv` provide an auditable beginning for a
  public field/value catalog.

The review also found editor and general-maintainability gaps that this story
must close:

1. `ConfigFile` is read-only. It stores normalized section/key maps and section
   order, but not comments, blank lines, original spelling, assignment style,
   disabled sample entries, or stable source spans. Re-serializing that model
   would destroy useful user-authored content.
2. There is no production API for a validated, atomic configuration update or
   for detecting that another process changed the file after the editor loaded
   it.
3. Validation metadata is spread across `MainConfigSchema`,
   `RendererProfileConfig`, runtime parsing, the HTML reference, and two TSV
   inventories. The TSV files do not describe every control's type, default,
   numeric range, units, omission behavior, applicability, or restart effect.
4. Several startup fields are currently accepted as `Any` and validated later
   or resolved by runtime code. A UI must not invent a second, weaker list of
   accepted values.
5. The checked-in sample historically used separate `[queue_recovery]` and
   `[lldv]` settings. Recovery is now canonical in the default Queue profile;
   LLDV is public and structured; logging remains preserved but advanced.
6. The parser treats `#` or `;` preceded by whitespace as a comment delimiter
   and has no general quoting/escaping layer. Structured edits must preserve
   untouched action commands and other advanced values byte-for-byte rather
   than parsing and regenerating them unnecessarily.

No syntax or startup-schema error was found in the current checked-in
`VideoProcessor.cfg`. The risks above concern safe editing, schema authority,
and future drift rather than a known failure of the shipped sample.

## Scope

### 1. Formatting-preserving configuration document

- Add a line/token document model alongside the existing runtime map. It must
  retain comments, blank lines, line endings, BOM/encoding, section/key
  spelling, `:` versus `=`, ordering, disabled/commented examples, and unknown
  content.
- Give each active setting a stable source span so a structured edit changes
  only that value or inserts one canonical entry in the appropriate section.
- Preserve every unedited shader, profile, action, rule expression, unknown
  section/key, and user comment exactly.
- Reject ambiguous edits when duplicate active sections/keys or malformed
  syntax prevent a unique target. Show the existing parser warning and do not
  overwrite the file.
- Detect external changes by identity/hash or last-write plus content check.
  Require reload or an explicit conflict decision instead of overwriting a
  newer file.
- Save through a same-directory temporary file, flush it, create a timestamped
  backup, and atomically replace the target. A failed validation or write must
  leave the original usable and report the backup/temporary-file disposition.

### 2. One authoritative UI schema

- Extend or generate a machine-readable public schema from the same rules used
  by production startup validation. For each structured field record section
  pattern, key, owner, label/help, type, canonical values, default/omission,
  range, units, applicability, and restart/rebuild behavior.
- Use that schema for controls, validation, and documentation/inventory
  coverage. Do not maintain an untested duplicate list in the editor.
- Keep Queue recovery and LLDV in the public inventory, preserve logging as
  advanced, and tighten `Any` fields where VP has a known public contract.
- Keep machine-specific names such as capture devices, renderers, monitors,
  LUTs, and paths discoverable where practical and editable when discovery
  cannot prove the complete set.
- Link each field to its `CONFIGURATION.html` help entry and keep automated
  coverage that fails when a public field/value lacks schema or help.

### 3. Standalone Windows application and notification-area behavior

- Build and ship a separate configuration executable beside VP. Direct launch
  shows the editor and creates one Windows notification-area icon (the tray
  beside the volume/network icons).
- Enforce one editor instance per interactive user. A second launch activates
  the existing window and passes it the requested config path/owner request.
- Closing the window may leave the tray process available according to an
  explicit, documented close behavior; the tray menu must provide Open and
  Exit and clearly identify the config file being edited.
- Direct launch defaults to the `VideoProcessor.cfg` beside the installed VP
  executable and also supports an explicit file path for portable/testing use.
  It must never silently select an unrelated working-directory file.
- The editor must work when VP is not running. It must not require capture or
  renderer initialization merely to inspect and validate configuration.

### 4. VP launch and configurable shortcut

- Add a named VP shortcut setting for opening the config editor, for example:

  ```ini
  [shortcuts]
  config_editor: Ctrl+S
  ```

  The default and final spelling are subject to user feedback; any collision
  with another registered VP/profile shortcut must be diagnosed rather than
  resolved arbitrarily.
- The VP command passes the exact resolved primary configuration path and its
  main-window identity to the editor. Do not surface or create the legacy
  `/vr_config` compatibility split in the normal editor workflow.
- If the editor is already running, activate its existing window instead of
  spawning another process.
- When launched from VP, present the editor as an owned, foreground window over
  VP and optionally modal relative to the VP control window, without pausing
  capture/rendering. VP must recover input if the editor exits or crashes; do
  not use global always-on-top behavior.
- Direct launch may discover a running VP instance and offer the same owned
  presentation, but absence or ambiguity must remain safe and non-blocking.

### 5. Structured editing surface

- Organize known settings by task/category rather than exposing a flat INI
  grid: Startup, Queue, VP Renderer, Viewports, LLDV metadata, Shortcuts,
  Shaders, and Actions. Never expose PPM or `[directshow.conversion]`.
- Render Booleans as check boxes, enums as selectors, bounded numbers as
  numeric controls with units/ranges, paths as browse controls, and discovered
  devices/renderers/monitors as selectors with an explicit custom-value path
  where required.
- Distinguish omitted/default/AUTO/explicit states. Removing an override must
  be a first-class operation and must not be represented by an invented value.
- Show validation and restart/rebuild impact before save. Saving while VP runs
  does not imply live application; offer an explicit restart action only with
  user confirmation.

### 6. Profiles, rules, shaders, and shortcuts

- Show configured queue, `vprenderer`, viewport, shader, and action variants in
  their file order with their labels/names and current `when` expressions.
- First-version profile/rule support may provide selectors for common
  `${key}` shortcuts and a validated advanced expression field for arbitrary
  `when` logic. A full visual expression builder is not required for initial
  acceptance.
- Provide shortcut/key-chord capture with conflict detection for VP commands,
  renderer selection, profile variants, and configured shader variants.
- Recognize the shipped NLS shader group and allow basic mapping of its Off,
  Classic, and Protected selections by updating only the relevant `when`
  shortcut expression.
- Do not provide full HLSL/GLSL or arbitrary shader-chain editing in the
  structured UI. Offer Open in text editor/Advanced access and preserve all
  shader settings and source references exactly when another setting is saved.
- Unknown/custom shader groups remain visible as advanced/manual entries and
  must survive every structured edit unchanged.

### 7. Whole-file validation and diagnostics

- Before replacement, parse the complete candidate file and run the exact
  aggregate, main, renderer/profile, shader, action, expression, and shortcut
  validation used by VP startup.
- Block save for an invalid candidate and show section, key, line/source span,
  received value, and accepted constraint where available.
- Provide a Validate command that makes no file changes and reports the config
  path, schema version/build identity, warnings, and success.
- After save, reload the file through the production reader and verify that all
  untouched active section/key/value pairs and advanced source spans remain
  unchanged except for the requested edits.

## Initial interaction proposal for user testing

The first usable build should emphasize feedback over complete visual polish:

1. tray/direct/VP launch and one-instance behavior;
2. General, Queue, DirectShow, Alpha/display, and shortcut controls driven by
   the schema;
3. a profile/shader list with shortcut capture and raw validated `when` text;
4. a read-only diff preview before save plus backup path; and
5. Advanced/Open-in-editor access for everything not yet structured.

Questions to collect during the first live trial include whether Ctrl+S is the
right default VP launch chord, whether closing should hide to tray or exit,
which categories belong on the first screen, and whether profile rules need a
visual condition builder beyond shortcut capture and validated expression
text.

## Explicit exclusions

- Do not introduce a second normal configuration file, registry settings, or a
  private editor database as configuration authority.
- Do not rewrite the whole file into canonical formatting after a small edit.
- Do not silently drop, reorder, normalize, enable, or disable manual shader,
  action, profile, comment, or unknown content.
- Do not add a full shader source/code editor in the first version.
- Do not promise live application for startup-only settings.
- Do not deploy or modify the active user's configuration as part of building
  the editor without separate explicit approval and a timestamped backup.

## Verification

1. Golden round-trip tests covering comments, blank lines, CRLF/LF, BOM,
   `:`/`=`, disabled keys, empty sections, unknown keys/sections, custom
   shaders, actions containing paths/arguments, and arbitrary rule expressions.
   A load/save with no edits must be byte-identical.
2. Targeted-edit tests proving only the intended source span changes and every
   advanced/manual span remains byte-identical.
3. Duplicate/malformed-input, invalid-value, failed-write, locked-file,
   external-change, backup, and atomic-replace recovery tests.
4. Schema/control coverage tests proving every structured control uses the
   production value/range/default contract and every public field is either
   structured or deliberately marked Advanced.
5. Shortcut tests for registration, conflicts, modifier/key capture,
   `config_editor`, renderer mappings, profile variants, and shipped NLS Off,
   Classic, and Protected mappings.
6. Standalone, tray reactivation, second-launch, VP-owned/modal, VP-crash,
   editor-crash, and VP-not-running behavior on Windows.
7. Validate and edit copies of the checked-in sample and the active deployment
   configuration. For the latter, use a disposable copy until explicit
   deployment approval.
8. Complete x64 Release build and test suite. Manually verify high-DPI layout,
   keyboard navigation, screen-reader labels, notification-area recovery after
   Explorer restarts, and operation without administrator rights.

## Acceptance criteria

- The editor runs independently and from a configurable VP shortcut, uses one
  notification-area instance, and opens over VP when VP requested it.
- The normal product and editor use one `VideoProcessor.cfg` and agree on the
  exact resolved path and validation result.
- Known values use suitable selectors/checkboxes/numeric/path controls rather
  than unrestricted text wherever the product has a closed contract.
- No-op save is byte-identical; a structured save preserves every unedited
  comment, advanced rule, shader setting, unknown entry, ordering choice, and
  formatting detail.
- Invalid, ambiguous, concurrently changed, or unwritable configuration never
  replaces the last usable file, and every successful change has a recoverable
  backup and visible diff.
- The shipped NLS selections and their shortcuts can be managed without
  exposing full shader editing; custom/manual shaders remain intact.
- Existing VP startup, capture, rendering, shortcut, profile, action, and
  configuration compatibility behavior has no regression.

## Related stories

- VP-0036: consolidated application and renderer settings into one file.
- VP-0049 and VP-0086: canonical and comprehensive configuration reference.
- VP-0079: canonical profile/queue/shortcut configuration model.

## 2026-08-06 Qt discovery and main-window inventory update

- Added a small v142 discovery bridge around VP's existing capture-device,
  renderer, and monitor discovery code. The v143 Qt editor loads this bridge
  dynamically, so its selectors use the same installed-hardware inventory as
  VP without duplicating enumeration logic.
- Unset capture device, renderer, and monitor settings remain unset and appear
  as `Not configured`/`Default monitor`; discovery does not silently persist
  the first detected item. Explicit configured values remain visible even if
  they are not currently detected.
- Added a structured DirectShow page for start/stop timing method, frame
  offset, video conversion, container/HDR policy, and renderer color
  overrides. Legacy DirectShow keys in `[general]` are displayed for
  compatibility and migrate to `[directshow]` when edited.
- Corrected `[directshow]` schema ownership: transfer-matrix and primaries are
  accepted, while unrelated startup keys are no longer accidentally accepted
  because of positional rule reuse.
- Recorded the main VP UI/config boundary in
  `docs/VP-0097_MAIN_UI_CONFIGURATION_INVENTORY.md`. Restart/reset commands,
  counters, current signal properties, timing readouts, and the CIE plot remain
  runtime UI state rather than configuration. Capture input, queue enable, and
  queue auto-reset still need an explicit durable contract before the editor
  can expose them.
- Switched the Qt application to Fusion styling and added a restrained custom
  combo-box/dropdown treatment instead of the legacy native Windows selector
  rendering.
- The x64 renderer selector always includes `VideoProcessor Renderer (Alpha)`;
  it is a first-class configuration choice even when a development copy of the
  editor is not running beside the Alpha plugin. Popup rows use the same
  spacing, hover, focus, and selection treatment as the rest of the editor.
- Product requirement remains that `[directshow.conversion]` and
  `[directshow.ppm]` are preserved but never exposed by this editor.
- Main-window HDR metadata numbers are live/session-only overrides when User
  luminance is selected, not persistent editor settings. Fullscreen controls
  likewise change live state while the config values only seed startup.
- Corrected frame-offset handling so the runtime accepts case-insensitive
  `AUTO`, matching the case-insensitive schema validation contract.
