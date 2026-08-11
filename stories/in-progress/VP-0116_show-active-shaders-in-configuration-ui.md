# VP-0116: Show all active shaders in the configuration UI

## Status

In progress. Observed in the deployed `v1.2.001-beta` configuration editor on
2026-08-11: the Shaders page lists Off and the configured NLS modes, but none
of the rows identifies the shader mode currently active in the attached VP
runtime.

## User story

As a VideoProcessor operator, I want the Shaders page to identify every shader
that is currently active in the running VP instance, so I can distinguish the
live shader chain from the row I merely selected for editing.

## Scope

1. Add the same compact runtime-active treatment used by other profile-aware
   lists to the Shaders page. Editing selection and runtime activity remain
   independent states and may appear on different rows.
2. Model shader activity as a set rather than a single display label. An
   exclusive/single shader group has exactly one effective active choice,
   including its Off/root choice when no member is active. A composable/multi
   group may have zero, one, or several active members at the same time.
3. Publish stable shader identities from the validated VP process to its
   associated configuration editor. Do not infer activity from row order,
   labels, shortcuts, the editor's current selection, or saved configuration
   alone.
4. Refresh the markers when runtime shader selection changes, when the active
   renderer changes, and after a safely applied configuration change alters
   the effective shader set.
5. Preserve the existing offline behavior: a standalone editor, stale runtime
   snapshot, or unresolved shader state shows no false active marker.
6. Keep this work renderer-neutral where the runtime has authoritative shader
   state. If a backend cannot report an effective set, report unavailable
   rather than guessing.

## Activity semantics

- `Active` means the runtime-resolved configured shader mode or member for the
  current renderer generation. It does not mean the row selected for editing.
- A temporarily bypassed NLS mapping may remain the selected/armed shader mode
  if that is the existing runtime contract; this story does not redefine
  per-frame NLS safety or mapping state.
- Off is a real effective state for an exclusive group and may receive the
  active marker.
- Multi-group activity must not be collapsed to one name merely to fit the
  current NLS-only UI.

## Acceptance criteria

- With NLS Off at runtime, the Off row has the active indicator even when a
  different row is selected for editing.
- Activating Nonlinear Stretch or Nonlinear Stretch Protected moves the active
  indicator to the correct row without changing editor selection.
- A synthetic or future composable shader group can expose two or more active
  members and every corresponding row is marked simultaneously.
- Switching renderer or changing shader selection refreshes the displayed set
  from a generation-current runtime snapshot; stale members from the previous
  renderer generation are cleared.
- Standalone Config use, a stopped/unreachable VP instance, and an unsupported
  backend show no guessed/default marker.
- Marker updates do not write configuration, reorder shader members, trigger a
  renderer restart, activate a shader, or disturb an open dropdown/current
  editing selection.
- Focused tests cover Off, one active member, multiple active members,
  selected-but-not-active, unavailable, stale-generation rejection, and live
  refresh. A clean x64 Release build and relevant Config/live-status tests
  pass.

## Non-goals

- Redefining shader-group matching, execution order, NLS safety bypass, or
  renderer shader semantics.
- Adding shader enable/disable controls beyond the existing configuration
  workflow.
- Treating saved configuration or shortcut assignment as proof of runtime
  activity.
- Expanding the Shaders page into a general shader-chain editor.

## Dependencies and references

- VP-0112: active-profile indicators and the process-scoped runtime status
  transport used by the configuration editor.
- VP-0028: unified profile, shortcut, and event-action configuration.
- VP-0051: generic Alpha shader-chain support and execution-order reporting.

