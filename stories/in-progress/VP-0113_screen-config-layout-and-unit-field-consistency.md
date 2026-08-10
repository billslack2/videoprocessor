# VP-0113: Screen Config layout and unit-field consistency

## Status

In Progress (2026-08-10). Implementation began after the developer confirmed
the discovered default branch `v1.2.001-beta`. Worktree:
`C:\\Users\\bslac\\vp\\worktrees\\vp-0113-screen-config-layout`, branch
`codex/vp-0113-screen-config-layout`, based on `origin/v1.2.001-beta` at
`0e74e038` after the queue-update merge.

Current work: inspect the Qt configuration editor's existing section-header,
collapse-state, and inline unit-field patterns; apply the appropriate shared
layout to Screen Config without changing its persistence or runtime behavior.

The user clarified that the section should be named **Subtitles**. Assess
whether the remaining lower-frequency controls merit an Advanced section, but
do not add one merely for symmetry.

## User story

As a VideoProcessor operator editing screen profiles, I want related Screen
Config options grouped under clear collapsible subheadings and numeric values
with units presented consistently, so the page is easier to scan and values
such as seconds and milliseconds do not appear misaligned.

## Observed issue

The reviewed Screen Config page presents its profile options as one long
details panel. The subtitle timing and pixel settings use full-width entry
boxes followed by unit labels at the far edge. Because unit labels have
different widths (for example, `seconds`, `ms`, and `pixels`), their visible
alignment is uneven and the inputs do not match the cleaner inline
value-and-unit treatment already used elsewhere in the editor.

The page also lacks the expandable/collapsible subheadings used successfully
by Renderer Setup to separate related configuration groups.

## Scope

1. Organize Screen Config profile details into concise, meaningful expandable
   subheadings using the established Renderer Setup section pattern. Keep
   directly related controls together; likely groups include profile/selection,
   picture geometry, and subtitle-fit behavior, subject to the current editor
   model.
2. Use the established inline numeric value-and-unit treatment for Screen
   Config fields whose unit is a fixed part of the setting. The unit must sit
   beside its entry box rather than at a variable far-right position.
3. Apply the same treatment to the relevant Screen Config timing and dimension
   fields, including subtitle hold, engage/release drift, padding, and target
   buffer when those fields remain exposed.
4. Audit comparable fixed-unit fields on the configuration pages touched by
   the shared component/style. Bring them into the same treatment only where
   doing so is semantically appropriate and does not disturb free-form or
   unitless settings.
5. Preserve all profile creation, ordering, default precedence, activation
   rule, validation, persistence, and safe live-apply behavior. Existing
   values and their configured units must not be converted or silently
   changed.
6. Keep the active-profile treatment introduced by VP-0112 intact and ensure
   collapsed sections do not obscure profile selection or active status.

## Acceptance criteria

- Screen Config has clear collapsible subheadings consistent with Renderer
  Setup's existing visual and interaction pattern.
- Related controls are grouped logically, and expanding/collapsing a group
  neither loses an edit nor changes the selected/active profile.
- Every relevant fixed-unit Screen Config input renders as one inline control
  with its unit immediately adjacent; `seconds`, `ms`, and `pixels` no longer
  produce visibly uneven far-edge labels.
- Relevant comparable editor fields use the same component/style, while
  free-form, choice, and unitless fields retain appropriate layouts.
- Saving and reloading preserves values, comments, ordering, and unknown
  configuration content exactly as before.
- Existing editor tests continue to pass, focused UI/component tests cover
  the unit presentation and section collapse state, and a clean x64 Release
  configuration-editor build succeeds.

## Non-goals

- Changing screen-profile matching, default precedence, shortcut behavior, or
  runtime activation.
- Changing subtitle detection, subtitle-fit algorithms, or the timing/pixel
  values themselves.
- Replacing the Screen Config page with a new profile-management workflow.

## Dependencies and references

- VP-0097: safe standalone configuration editor and VP integration.
- VP-0110: viewport subtitle placement timing values shown on this page.
- VP-0112: active-profile indicator that must remain visible and independent
  of the profile selected for editing.
- User-provided Screen Config screenshot, 2026-08-10.
