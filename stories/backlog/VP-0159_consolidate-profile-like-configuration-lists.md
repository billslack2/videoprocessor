# VP-0159: Consolidate profile-like configuration lists on one shared UI component

## Status

Backlog (2026-08-28). Begin with a code-level inventory and architecture
review. No implementation should start until a principal UI engineer has
reviewed the inventory, the required semantic variants, and the proposed
shared-component boundary.

## User story

As a VideoProcessor maintainer, I want profile and profile-like configuration
lists to use one robust shared UI component, so identical behavior is
implemented once while real differences between profiles, shaders, NLS, and
actions remain explicit and testable.

## Problem

The configuration editor presents several list-oriented surfaces that look and
behave similarly, but their implementations and feature sets may have evolved
independently. True profile pages must not each retain their own copy of common
profile-list behavior. NLS/shader and Action pages use similar list management
patterns but do not have identical semantics and currently miss some of the
capabilities or consistency available elsewhere.

Before consolidating code, establish which pages share identical behavior,
which merely duplicate implementation, and which intentionally differ. In
particular, Actions are not profiles, and shader/NLS groups may be exclusive,
composable, inactive, or multiply active. A shared component must express
those differences through a clear model, capabilities, or adapters rather
than pretending every list contains profiles.

## First deliverable: implementation inventory and review

1. Inventory every configuration-editor profile and profile-like list,
   including Rendering, Color Config, Output, Screen Config, Input Processing,
   Queue, shader/NLS, Actions, and any other surface discovered in code.
2. For each surface, identify the concrete UI class/component, backing model,
   event and validation wiring, persistence/live-apply path, tests, and any
   copied helpers or handlers.
3. Classify each behavior as:
   - identical functionality backed by shared code;
   - identical functionality implemented more than once;
   - visually similar functionality with deliberately different semantics; or
   - an accidental feature or behavior gap.
4. Produce a deviation matrix covering at least creation, deletion,
   duplication, rename, ordering, enabled state, default/root/Off treatment,
   edit selection versus runtime-active state, single versus multiple active
   members, validation/error display, empty state, keyboard/focus behavior,
   unsaved-change handling, persistence, and live apply.
5. For every deviation, recommend **retain**, **normalize**, or **investigate**
   and state the evidence. Explicitly document why Actions remain actions and
   how shader/NLS semantics differ from ordinary single-selection profiles.
6. Have a principal UI engineer review and approve the inventory and proposed
   component boundary before production refactoring begins. Record decisions
   and unresolved questions in this story or a linked repository document.

## Implementation scope

1. Introduce one shared list-management UI component for the behavior proven
   common by the inventory. Prefer a small semantic model plus explicit
   capability flags or adapters over page-specific branches scattered through
   the component.
2. Migrate every true profile page to the shared component and remove the
   superseded duplicate implementations.
3. Reuse the same component or its common lower-level list primitive for
   shader/NLS and Actions only where the inventory proves the interaction is
   genuinely shared. Preserve their distinct models and commands.
4. Close accidental feature gaps for shader/NLS and Actions when the retained
   semantic matrix says the shared behavior applies. Do not add profile-only
   concepts such as an active profile to Actions.
5. Preserve existing configuration schemas, ordering, labels, shortcuts,
   validation, persistence, live-apply behavior, and runtime status contracts
   unless a deviation is explicitly approved for normalization.
6. Add focused component-contract tests and retain page-level regression tests
   for every supported semantic mode.
7. Obtain a principal UI engineer code/architecture review of the completed
   consolidation before acceptance.

## Constraints and non-goals

- This is a code-quality and behavioral-consistency change, not a redesign.
- Do not engage a visual designer or create a new visual language for these
  pages.
- Preserve the current layout, terminology, and ordinary interaction flow
  except for approved fixes to accidental inconsistencies or missing features.
- Do not merge the underlying profile, shader/NLS, and Action domain models.
- Do not change profile matching, shader execution, NLS runtime policy,
  action scheduling/execution, or configuration-file semantics merely to make
  the UI component uniform.
- Do not leave compatibility wrappers that continue to contain independent
  copies of the same list behavior.

## Acceptance criteria

1. A checked-in inventory names every relevant surface and implementation
   path, identifies shared code and duplicate implementations, and includes an
   evidence-based deviation/retention matrix.
2. The inventory and proposed API are reviewed by a principal UI engineer
   before refactoring, with the decisions recorded.
3. All true profile-list pages use one shared component and no separate copy
   of common create/delete/duplicate/rename/reorder/selection behavior remains.
4. Shader/NLS and Action pages reuse the common primitive for approved shared
   behavior while retaining explicit, tested semantic modes. Actions are never
   reported or treated as profiles; exclusive, composable, Off, and
   multi-active shader/NLS states remain representable.
5. Retained differences are expressed through documented model contracts,
   capabilities, or adapters rather than page-name checks or copied event
   handlers.
6. Existing visible layout and terminology remain materially unchanged. Any
   normalized behavior or closed feature gap is traceable to the approved
   deviation matrix.
7. Component tests cover each semantic mode and page-level regressions cover
   editing selection, runtime-active state, list mutation, validation,
   persistence, live apply, keyboard/focus behavior, and offline/unavailable
   runtime status where applicable.
8. A clean x64 Release build and the complete relevant configuration-editor
   test suite pass, followed by principal UI engineer review of the final
   architecture and removal of duplicate code.

## Dependencies and references

- VP-0097: safe standalone configuration editor and VP integration.
- VP-0112: active profile indicators and the distinction between edit
  selection and runtime-effective state.
- VP-0116: shader sets, Off state, composable members, and multiple active
  rows.
- VP-0152: independent profile families, Actions, and coordinated live apply.
- VP-0158: meaningful profile/queue/NLS reporting and single-option
  suppression.
