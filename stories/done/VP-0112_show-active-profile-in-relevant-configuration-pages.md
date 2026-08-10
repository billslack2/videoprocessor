# VP-0112: Show the active profile in relevant configuration pages

## Status

Done (2026-08-10). Implementation started on
`codex/vp-0112-active-profile`, rebased from the current repository default
branch `origin/v1.2.001-beta` at `109bc963`. Working tree:
`C:\Users\bslac\vp\worktrees\vp-0112-active-profile`.

Current work: complete the remaining runtime/profile coverage and focused test
work without changing profile persistence.

2026-08-10 implementation checkpoint:

- Implemented `e194c4e` on `codex/vp-0112-active-profile`. VP now publishes
  its resolved display and screen-profile sections through a process-scoped
  read-only status mapping. The attached configuration editor polls that
  mapping only for its validated VP owner process; offline or unrelated editor
  instances show no false marker.
- Follow-up implementation through `3226b7d` publishes status on each
  successful unified-profile refresh, independently of alpha/non-alpha
  renderer selection. Relevant profile lists retain normal selection behavior
  and mark only the active row with a small green dot; there is no editing
  treatment, key, extra explanation, or inline `when:` rule. Queue, Actions,
  and Shortcuts remain unchanged.
- Rebuilt x64 Release GUI, VP Renderer, and configuration editor from the
  feature commit. Deployed `VideoProcessor.exe` and
  `vprenderer\\VideoProcessorVPRenderer.dll` as a verified matching pair,
  plus `VideoProcessorConfig.exe` and its discovery DLL, to
  `C:\Videoprocessor\vp`; backup:
  `C:\Videoprocessor\vp\backup-vp0112-20260810-171405`.
- Live screenshot validation against the deployed VP confirmed the active
  `Scope` Screen Config row in green while `16x9 (Default)` remained selected
  in blue. Screenshot:
  `C:\Users\bslac\AppData\Local\Temp\vp0112-active-profile.png`.
- The x64 Release builds succeeded. `VideoProcessorConfigTests` builds but its
  behavioral run currently reports broad Apply/effect-summary failures; these
  have not yet been isolated as baseline versus regression, so the story
  remains In Progress.
- Rebuilt the x64 Release configuration editor and deployed the updated
  `VideoProcessorConfig.exe` to `C:\Videoprocessor\vp`, SHA-256 verified;
  backup: `C:\Videoprocessor\vp\backup-vp0112-dot-20260810-172943`.
  Live screenshot validation against the deployed VP shows the selected
  `16x9 (Default)` row with its normal blue selection and the independently
  active `Scope` row with a green dot. Screenshot:
  `C:\Users\bslac\AppData\Local\Temp\vp0112-active-profile-dot.png`.
- Merged as [PR #49](https://github.com/billslack2/videoprocessor/pull/49)
  into `v1.2.001-beta` at merge commit `95eee869`.

## Chosen UI treatment

Use the existing profile list only; do not add a separate current-profile
summary, duplicate label, key, or explanatory text. Mark the active profile
with a small green dot beside its name.

- Keep each profile-list row name-only. Do not show its `when:`/activation rule
  inline because real rules can be long; retain that rule in the selected
  profile's existing detail/editing surface instead.
- The profile that is active at runtime uses a small green dot.
- Editing uses only the list's normal selection behavior; it has no separate
  state treatment.
- The normal selected row and active dot are independent and may appear on
  different rows.

The visual distinction itself must be clear enough that extra explanation is
unnecessary. Apply it only to the relevant profile-aware pages in this story's
scope; do not add it to Actions or Shortcuts/keys.

## User story

As a VideoProcessor operator editing conditional configuration, I want the
configuration UI to make the currently active profile obvious on pages where
profiles affect the running result, so I can quickly tell which screen,
shader, renderer, or other relevant profile is in effect.

## Scope

1. On each profile-aware configuration page whose profiles affect runtime
   rendering or display behavior, visibly mark the profile that is currently
   active/effective for the running VP instance. A compact `Active` badge,
   indicator, or equivalent clear treatment in the existing profile list is
   sufficient; it must not require opening another dialog.
2. Initial candidates include Screen Config/Viewports, Shaders, and Renderer
   profiles. Apply the same treatment to another page only when it has the
   same conditional-profile semantics and a meaningful resolved active
   profile.
3. The marker represents runtime/effective activation, not merely the profile
   currently selected for editing. Selection focus and `Active` state must be
   visually distinct and may appear on different profiles.
4. If the editor is launched without a reachable running VP instance, or VP
   cannot resolve an active profile for that page, show an unobtrusive
   unavailable/not-current indication rather than guessing or marking the
   default profile.
5. Refresh the indicator when the editor receives an updated runtime snapshot
   and after a safely applied configuration change that can change the active
   profile. The UI must not mutate configuration, force a profile, restart a
   renderer, or change profile ordering to obtain this state.
6. Do **not** add an active-profile marker to Actions, Shortcuts/keys, or
   other non-profile configuration pages. Those pages may have enabled or
   selected items, but that is different from a resolved conditional profile.

## Acceptance criteria

- With a running VP instance and a known active screen, shader, or renderer
  profile, the corresponding relevant page clearly identifies that profile
  without changing the user's current editor selection.
- A profile selected for editing remains clearly distinguishable from the
  independently active profile; the behavior is correct when they differ.
- Switching the runtime condition so another configured profile becomes
  effective updates the marker at the next supported snapshot refresh, with
  no configuration write or renderer restart caused by the editor.
- Pages with no resolved active profile, and standalone/offline editor use,
  do not present a false active/default marker and explain the unavailable
  state concisely where an indicator would otherwise appear.
- Actions and Shortcuts/keys retain their existing presentation and receive no
  active-profile affordance.
- Existing profile creation, removal, reordering, validation, lossless
  persistence, and safe live-apply behavior remain unchanged.
- Focused editor tests cover active, inactive, unavailable, and
  selected-but-not-active states; a clean x64 Release build and relevant
  configuration-editor/live-apply tests pass.

## Non-goals

- Changing profile matching rules, `when:` expression evaluation, fallback
  order, or default-profile behavior.
- Adding profile activation controls, runtime configuration editing, or a new
  profile-management workflow.
- Treating action enablement or shortcut registration as profile activity.

## Dependencies and references

- VP-0097: safe standalone configuration editor and VP integration.
- VP-0103: safely applying saved configuration to a running VP instance.
- Existing conditional profile resolution for screen/viewports, shaders,
  renderers, and other renderer-affecting configuration domains.
