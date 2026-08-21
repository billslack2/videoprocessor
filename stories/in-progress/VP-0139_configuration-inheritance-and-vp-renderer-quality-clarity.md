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

Focused Config tests and each deployed x64 Release Config build passed for
these checkpoints. Continue operator validation and record each subsequent
concern, source commit, and release validation here before moving the story to
Review.

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
