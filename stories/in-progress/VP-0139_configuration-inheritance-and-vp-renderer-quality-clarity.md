# VP-0139: Configuration inheritance and VP Renderer quality clarity

## Status

In Progress (2026-08-21). Operator review identified misleading configuration
editor states while validating the deployed x64 Release Config application.
Implementation is active in
`C:\Videoprocessor\vp\vprenderer\.codex-worktrees\vp-lldv-singleton-fix`
on branch `codex/fix-lldv-singleton`; it has not yet been committed as a
dedicated VP source change.

Completed and deployed checkpoints:

- DirectShow and VP Renderer Input Processing selectors now display the
  effective General value when their renderer override is omitted, including
  General's implicit `Follow input` container-color policy.
- The inherited choice itself remains resolved beside an active override and
  refreshes while General is edited in the same Config session.
- VP Renderer root-profile selectors that offer `Auto` no longer expose a
  redundant empty `Use default` choice; `Auto` is selected instead. Named
  profiles retain an inheritance choice.
- Root Rendering quality now always selects an explicit quality and defaults
  to `High`; its visible order is `High`, `Balanced`, `Fast`.
- Config now overwrites an externally edited configuration on Apply or OK.
  The external version is retained first as Config's timestamped backup; the
  editor still validates its document and performs the atomic replacement.
  Other non-editor `SaveSafely` callers retain their conflict guard.
- The VP Renderer scaling review confirmed a material option-parity gap:
  libplacebo 7.360.1 exports substantially more scaler filters than Config
  exposes. Config, validation, and the renderer filter mapping now expose the
  documented scaler set (including explicit disabled/built-in sampling), with
  each explicit filter covered by a native-export regression test.
- VP Renderer now serializes libplacebo's resolved render options after
  initialization, live profile application, renderer start/restart, and reset.
  This makes the selected upscaler/downscaler and complete libplacebo option
  state observable after renderer swaps and HDMI graph re-syncs.
- Scaling and Processing selectors now present explicit values in quality-first
  order after `Auto`. The downscaler's final `none` value is correctly shown
  as `Match upscaler` and now resolves to the active upscaler rather than
  incorrectly disabling downscaling. The user-facing sigmoid control is
  labelled `Anti-ringing` with an accurate explanatory tooltip.
- Peak detection now presents `High quality`, `On`, then `Off` after `Auto`.
  Shader preparation no longer disables Config while waiting for/working in
  VideoProcessor; Config starts non-capturing preparation when possible and
  reports a failed or stale request rather than blocking indefinitely.
- The shared LLDV page no longer presents metadata handling as an optional
  checkbox: it is always active when the page's metadata is configured. The
  former `Use new LLDV detection` switch is now labelled `Alternate LLDV
  detection`; its existing persisted `general.newlldv` behavior is unchanged.
- The initial tone-mapping comparison was only a high-level inventory and is
  superseded by an API-7.360 registry audit. VP must use libplacebo's exported
  `pl_option_list` (key, type, range, deprecated and preset metadata) as the
  exact contract before deciding which controls are basic versus Advanced.
  The current UI exposes only core target levels, tone/gamut choice,
  peak-detection policy, and contrast recovery; it does not represent the
  complete `pl_options` interface.
- The deployed `libplacebo-360.dll` was queried directly: API 7.360 exposes
  215 option entries. This is a library capability registry, not a VP feature
  inventory. For example, VP calls `pl_render_image`, not
  `pl_render_image_mix`, so frame-mixer controls would be no-ops; other
  entries require host frame metadata, VP-owned LUT/presentation integration,
  or are explicitly deprecated/debug-only. Any Advanced page must be a
  typed, version-gated VP manifest rather than an arbitrary `pl_options_load`
  pass-through.
- The audit found two existing UI/adapter corrections to prioritize before
  additional surface area: `contrast_recovery` is valid through `2.0` (VP
  presently limits it to `1.0`), and Peak detection needs distinct Auto,
  Standard/default, High quality, and Off states. Current `On` and
  `High quality` both resolve to libplacebo's high-quality preset.
- Config now shows a single italic, value-only Auto resolution line below the
  reviewed Basic controls (for example, `EWA Lanczos sharp`, `Spline`, or
  `Perceptual`). The longer `Auto`, quality, applicability, and runtime-state
  wording was removed after operator review. Changing Rendering quality still
  immediately recalculates every Auto field; explicit choices remain fixed.
- The Auto presentation now covers every Auto-capable VP Renderer control in
  Rendering, Source transfer, Tone mapping, Scaling and Processing,
  Calibration LUT reference, and Advanced output. Fixed policies show their
  resolved value (for example `sRGB`, `Full RGB`, `Flip`, or `2.4` when
  Limited is selected); source- and device-dependent controls accurately show
  `Source metadata`, `Source dependent`, `Output format`, or `Not
  constrained` rather than a fictitious fixed value. Display primaries no
  longer has a separate `Default: Rec. 709` choice: it has only Rec. 709 and
  BT.2020, selecting Rec. 709 when unset.
- Operator review standardized the concise italic labels as `Auto: <effective
  value>` (for example, `Auto: Source dependent`) so it is explicit that the
  displayed value is the current Auto policy rather than a saved override.
- `contrast_recovery` now validates and accepts the complete libplacebo range
  `0.0` through `2.0`; Config labels and placeholder text match that range.
  Peak detection now has distinct native states: `Auto` keeps the quality
  preset, `Standard` uses `pl_peak_detect_default_params`, `High quality`
  uses `pl_peak_detect_high_quality_params`, and `Off` disables it. The
  persisted legacy `on` spelling remains compatible and is displayed as
  `Standard`.
- The Auto scaler policies are protected by a native-parameter regression
  test against the deployed libplacebo API: High is EWA Lanczos sharp up /
  Hermite down, Balanced is Lanczos / Hermite, and Fast uses built-in sampling
  for both. This verifies the Config effective-value labels and confirms that
  Hermite is the native High-preset downscaler, rather than a VP fallback.

Focused Config tests and each deployed x64 Release Config build passed for
these checkpoints. The Auto-preview and mapping change was built from x64
Release, verified by the Config regression plus three native libplacebo
parameter tests, backed up/deployed, and Config was reopened for operator
validation. Continue the remaining field-by-field review and record each
subsequent concern, source commit, and release validation here before moving
the story to Review.

## User story

As a VideoProcessor operator, I need Config to show the value a renderer will
actually use and to present VP Renderer quality policies without duplicate or
ambiguous defaults, so I can make intentional changes without confusing
inheritance, `Auto`, and an omitted setting.

## Scope

1. Make omitted renderer Input Processing overrides visibly resolve to their
   General parent value, and refresh that presentation when General changes.
2. Keep renderer-specific overrides optional: showing a resolved inherited
   value must not write an override into the configuration file.
3. Remove redundant root-profile empty/default choices where `Auto` is the
   quality-governed VP Renderer policy; retain named-profile inheritance.
4. Make root Rendering quality an explicit, ordered choice: `High`,
   `Balanced`, `Fast`, defaulting to `High`.
5. Add focused Config editor regression coverage and deploy only from a
   successful x64 Release build for operator testing.

## Acceptance criteria

1. An omitted DirectShow or VP Renderer input override displays the current
   effective General value, including `Follow input`, without persisting a
   backend key.
2. Changing a General input policy while Config is open immediately refreshes
   all inherited renderer choices, including their inactive inherit row beside
   an explicit override.
3. VP Renderer root-profile selectors with `Auto` do not show a separate
   selectable `Use default`/`Default: Auto` row; named profiles can still
   inherit their root value.
4. Root Rendering quality has no selectable `Default: High` row, selects
   `High` when unset, and lists `High`, `Balanced`, `Fast` in that order.
5. Focused Config editor tests and a clean x64 Release Config build pass;
   deployment retains backups and is accepted by operator validation.

## Boundaries and dependencies

- This story covers the reviewed Config presentation and persistence behavior,
  not an exhaustive libplacebo option audit. VP-0128 remains the authoritative
  broader option-parity and resolved-defaults audit. The 2026-08-21 scaling
  review and selected-filter expansion complete the initial scaler subset;
  VP-0128 continues to own broader option coverage and resolved-default work.
- VP-0123 owns the underlying per-renderer Input Processing policy split; this
  story improves its interactive inherited-value presentation.
- Preserve existing configuration comments and explicit settings. Never turn a
  displayed inherited/default value into a saved override merely by opening the
  editor.

## Implementation checkpoint — 2026-08-21

- Completed the inherited Input Processing presentation and immediate General
  value refresh without persisting displayed inherited values.
- Removed redundant root-profile default rows and made root rendering quality
  explicit, ordered `High`, `Balanced`, `Fast` with `High` as the unset value.
- Added compact italic resolved-policy text for every Auto-capable VP Renderer
  control. The text is nested directly below its selector (2 px spacing), not
  a separate blank form row. It uses the form `Auto: <effective value>`.
- Corrected target-black Auto to show its actual libplacebo calculation:
  target white divided by the SDR contrast constant (1000). For example,
  79 nits displays `Auto: 0.079 nits`, and refreshes when target white changes.
- Focused Config editor regression test passes. Successful x64 Release Config,
  main application, and VP Renderer plugin binaries were deployed with
  timestamped binary backups; no active configuration was overwritten.

## Responsiveness checkpoint — 2026-08-21

- Normal VP Renderer startup no longer compiles inactive NLS profiles on the
  live render path. This avoids cold-cache shader compilation stalls during
  first playback. Config's existing **Prepare shaders** workflow remains the
  explicit, separately reported way to warm those profiles.
- Auto source-transfer text now consumes the live VP source snapshot. It shows
  the active transfer (for example `BT.1886` for a live SDR source) and says
  `Source unavailable` when VP has no live input instead of implying a source
  is known.
- A non-activating in-video status now says `Preparing VP Renderer shaders...`
  while a new NLS pipeline variant can compile. It is usable in both fullscreen
  and windowed presentation and clears when the render call returns.
- Removed the redundant Scaling and Processing summary text above its controls.
