# VP-0112: Show the active profile in relevant configuration pages

## Status

Backlog (2026-08-10). Requested as a small configuration-editor clarity
improvement. No implementation has started.

Next action: identify the editor's existing profile-list model and the
available runtime/effective-configuration snapshot, then add the smallest
shared active-profile presentation path that does not change profile selection
or configuration persistence.

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
