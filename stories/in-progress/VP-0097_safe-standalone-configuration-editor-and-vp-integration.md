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
5. The checked-in sample contains supported `[queue_recovery]`, `[lldv]`, and
   `[logging]` settings that are absent from the current public-field inventory
   and `CONFIGURATION.html`. The editor schema/reference coverage must either
   classify and document them as public or deliberately classify them as
   advanced/internal; they must not silently disappear.
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
- Reconcile the current `[queue_recovery]`, `[lldv]`, and `[logging]` inventory
  gap and tighten `Any` fields where VP has a known public value contract.
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
  grid: General/startup, Queue/timing, DirectShow/conversion, HDR/LLDV,
  Alpha/display, Profiles/viewports, Shortcuts/shaders, Actions, and Advanced.
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
