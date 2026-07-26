# VP-0028: Renderer configuration profile, hotkey, and event-action unification

## Status

In Progress. Documentation-first implementation started on
`codex/vp-0028-config-docs` in
`C:\Users\bslac\vp\videoprocessor-vp-0028`, based on confirmed integration
base `origin/v1.1.014-beta` at `fc3cd35`.

The first deliverable is a reviewable proposed configuration reference and
sample configuration. It documents the unified profile, binding, and
post-transition event model without changing runtime parsing or behavior.
The documentation is committed as `9b5c699` (`docs: propose unified renderer
configuration model`) on the tracked remote branch.

Review deliverables:

- `docs/VP-0028_RENDERER_CONFIGURATION_PROPOSAL.md`
- `docs/examples/VideoProcessorRenderer.unified.proposed.cfg`
- `VideoProcessorRenderer-Proposed.html` (review-only HTML help, committed as
  `fc303db`, then revised in `036aa10` to place shortcuts and persistence in
  profile/group configuration rather than a separate binding section, and in
  `2d1ffa6` to use the single `shortcut=` key in both contexts; revised again
  in `3b86cee` so hotkeys are ordinary `$key` conditions within `when=`)

Runtime implementation began on 2026-07-26 at the user's direction. The first
increment adds profile-section discovery, shared `$key` expression vocabulary,
automatic per-group selection, and accelerator discovery from `$key` clauses
while retaining the legacy display-rule paths. Focused rule tests and the
libplacebo project build pass. A GUI Debug build is currently blocked before
these changes by the worktree's missing generated `version.h`.

A subsequent multi-pass design/code review determined that this increment is a
prototype and must not be treated as the accepted unified implementation. It
still routes manual selection through one legacy display-rule slot, uses GUI
regex shortcut discovery instead of full AST evaluation, does not implement
independent group persistence or strict unified validation, and does not include
viewport/event-action behavior. The proposal and examples are being refined
before further runtime integration.

For future manual testing, a side-by-side configuration was created at
`C:\Videoprocessor\vp\VideoProcessorRenderer.vp0028-test.cfg`; the active
`VideoProcessorRenderer.cfg` was not edited. Do not use it to judge the unified
feature until the resolver, validation, persistence, and rebuild milestones
below are complete.

## Deep-review implementation boundary

Further implementation follows one testable pipeline:

```text
parse -> validate -> select independently per group -> resolve typed settings
      -> compare effective fingerprint -> apply -> persist committed state
```

Required next milestone:

1. A platform-independent `RendererProfileConfig` and reusable expression AST.
2. An explicit ordered profile list and independent automatic/manual selection
   for every group.
3. Strict schema/ownership validation. An explicit `config_version` or unified
   section selects unified mode; an omitted version in that mode means the
   latest supported unified schema, while an explicit version protects future
   migrations.
4. Full source-plus-`$key` evaluation with no GUI regex grammar.
5. Versioned, atomic, per-group persistence committed only after successful
   application.
6. Effective-settings fingerprints for correct rebuild/no-rebuild decisions.
7. Table-driven resolver, key, state, rollback, and compatibility tests.

Viewport integration and the generation-scoped refresh event scheduler follow
that milestone. Legacy syntax remains on a separate unchanged compatibility
path; mixed legacy/unified configuration is rejected.

Later review passes further require:

- explicit ordered `profiles=` lists and a complete v2 key/type/range/owner/apply
  schema;
- application-local accelerator behavior, full AST chord discovery, and
  one-request-per-physical-press repeat suppression;
- side-by-side `/vr_config` state isolation while preserving the legacy
  `screen_profile=` mirror for the default configuration;
- `program=`, literal `arguments=`, and optional `working_directory=` event
  actions with documented Unicode and batch-file launch behavior;
- non-persistent input/scaling diagnostic overrides in the examples;
- explicit full-comparison `||` event expressions, not legacy `|` shorthand;
- golden scenarios with exact selection, state, fingerprint, event, and log
  expectations; and
- no claim of mechanical migration for legacy rules spanning multiple unified
  setting owners.

## User story

As a VideoProcessor user, I want renderer profiles, automatic selection,
manual hotkeys, viewport choices, and refresh-triggered actions to follow one
consistent, documented configuration model, so I can understand what selects a
profile, what an input change can change, and exactly when an external action
runs.

## Current evidence

`VideoProcessorRenderer.cfg` currently combines four distinct patterns:

1. `[display_rules] rules=...` plus `[display_rules.name]` sections select one
   automatic renderer override by a source-state expression.
2. A `shortcut=` inside a display-rule section makes that same rule manually
   selectable, including the unsupported-in-spirit workaround of using an
   impossible expression such as `$width==0` for a manual-only profile.
3. `[shortcuts]` holds fixed action keys for `screen_profile_normal`,
   `screen_profile_scope`, and `display_rules_auto`; screen profiles are stored
   separately in `VideoProcessorRenderer.state` and do not participate in
   display-rule selection.
4. `[refresh_rate_commands]` maps truncated *resulting display rates* to shell
   command lines and delay settings. It is evaluated after refresh-rate
   handling rather than through the profile rule evaluator, and it retains a
   legacy `command=` plus argument-map format.

This makes otherwise similar concepts look like special cases. It also leaves
two overlapping documents (`VideoProcessorRenderer.html` and
`VideoProcessorRenderer-Alpha.html`) with different levels of detail and
outdated statements, including a Rec.709-only description despite selectable
BT.2020 targets.

## Scope

Define and implement one renderer-configuration vocabulary for:

- named profiles and inherited renderer settings;
- automatic source-state selection;
- manual profile selection and return to automatic selection;
- named viewport/screen choices and their hotkeys;
- post-transition actions, including refresh-rate-triggered commands; and
- one authoritative user-facing configuration reference with concise examples.

The model must explicitly distinguish **selection conditions**, which use input
state and choose rendering behavior, from **event conditions**, which run only
after a renderer/display transition has completed. A resulting output refresh
rate must never select a renderer profile, because that would create a
selection/switching feedback loop.

## Non-goals

- Do not change Alpha rendering, color management, cadence policy, or the
  physical display configuration merely to reorganize configuration.
- Do not turn external command execution into arbitrary per-frame scripting.
- Do not silently rewrite a deployed configuration or its state file.
- Do not remove a supported legacy key without a migration path and a release
  decision.
- Do not use an impossible source condition as the new representation of a
  manual-only profile.

## Required design decisions

1. Define a single named-profile schema. A profile must be able to have:
   - inherited renderer/output settings;
   - an optional source-state `when` condition for automatic eligibility;
   - an optional `$key` equality branch in the same parsed expression for
     manual eligibility; and
   - deterministic priority/specificity semantics when more than one profile
     is automatically eligible.
   A profile without `when` must be manual-only by declaration, not by a fake
   rule expression.
2. Use one parsed expression AST for source selection, key discovery, and key
   event evaluation. The GUI only delivers canonical key events. A canonical
   chord targets exactly one profile or group reset; duplicate/unregistrable
   chords are startup errors. Existing `[shortcuts]` remains legacy-only.
3. Define a separate event-action schema. It must support a post-refresh event
   matched against the actual accepted/restored display rate, with a bounded
   delay and a command action. Specify whether actions run on initial setup,
   an actual rate change, a confirmed unchanged rate, restoration at renderer
   shutdown, renderer rebuild, and failed refresh requests. Suppress duplicate
   launches for one logical transition/generation.
4. Preserve the important separation: source-state variables may select a
   profile; output/display facts may be observed by an event action only after
   the transition. Document the available variables and their exact rate
   representation. Replace truncated-rate ambiguity with an explicitly named
   compatibility behavior or a precise nominal/actual-rate representation.
5. Decide how screen/viewport state composes with display profiles. The user
   must be able to see the effective automatic/manual profile, viewport choice,
   active hotkey binding, and pending/last event action in diagnostics without
   implying that unrelated states are one merged rule.
6. Specify configuration discovery, inheritance, duplicate/unknown-key errors,
   invalid command handling, quoting/UTF-8 behavior, and command-security
   boundaries. Continue launching only after explicit user configuration; log
   the action identity and resolved condition, never sensitive command content
   beyond the existing documented behavior.
7. Provide a backward-compatible migration plan for `[display_rules]`, rule
   `shortcut=`, `[shortcuts]`, and `[refresh_rate_commands]`, including the
   legacy two-part `command=` format. Legacy configurations must retain their
   behavior for the supported release window, receive actionable deprecation
   diagnostics, and never be modified automatically.

## Implementation plan

1. Write and review the proposed configuration reference and parser-validated
   sample configuration before runtime implementation. Make the old/new
   compatibility boundary and the distinction between source selection and
   completed-transition events unambiguous.
2. Build a normalized configuration representation for profiles, bindings, and
   event actions before renderer creation. Keep parsing/validation outside the
   render loop and report errors before capture/renderer startup as appropriate.
3. Replace the separate display-rule, fixed-shortcut, and refresh-command
   selection paths with shared selection and binding services while preserving
   the renderer's existing safe-rebuild boundary.
4. Assign each source transition and renderer generation a stable identifier so
   a post-refresh action can run once for the intended completed transition and
   cannot outlive a rebuild, renderer shutdown, or profile replacement.
5. Update state persistence so persisted manual choices have stable named
   identities and stale/removed names safely fall back to configured automatic
   behavior. Preserve existing `VideoProcessorRenderer.state` semantics during
   migration.
6. Update the checked-in `VideoProcessorRenderer.cfg` to show one coherent
   baseline: automatic profiles, declared manual-only profiles, viewport
   bindings, and a post-refresh command action.
7. Consolidate documentation: make `VideoProcessorRenderer.html` the
   authoritative reference and reduce `VideoProcessorRenderer-Alpha.html` to
   non-duplicated conceptual guidance or a clearly maintained companion.
   Correct stale output-gamut statements and document actual-versus-requested
   display transition behavior.
8. Add concise OSD/log diagnostics for effective profile source (automatic or
   manual), viewport, selected binding, display transition result, action
   eligibility, scheduling, execution result, suppression reason, and legacy
   configuration use.

## Verification

- Unit-test parsing, inheritance, priority/specificity ties, manual-only
  profiles, binding syntax/conflicts, unknown/duplicate keys, and legacy
  configuration translation.
- Test automatic-to-automatic, automatic-to-manual, manual-to-manual, and
  manual-to-automatic transitions across SDR/PQ/HLG, resolution, interlace,
  and input-rate changes.
- Test viewport selection independently and in combination with profile
  selection; verify persisted state, removed-profile fallback, F2/F3/F4
  compatibility, and configured rule hotkeys.
- Test refresh actions at startup, a real applied change, an unchanged/confirmed
  rate, failed/unsupported switching, renderer rebuild, fullscreen/window
  transition, profile change, and shutdown restoration. Verify exactly-once or
  deliberate-suppression behavior for each documented event.
- Test 23.976/24, 29.97/30, 50, and 59.94/60 handling and prove the command is
  matched to the documented actual/accepted display fact rather than an
  ambiguous source or requested rate.
- Confirm no event action is invoked from the render loop or per frame, no
  command survives a discarded renderer generation, and malformed commands
  fail safely with actionable log entries.
- Validate the checked-in examples against the parser and review both HTML
  documents for consistent terminology, defaults, supported output targets,
  and migration instructions.
- Use `C:\logs\vp_debug.log` as the primary run-time evidence location and
  record relevant test excerpts/results in this story before Review.

## Acceptance criteria

- Users configure profiles, hotkeys, viewport choices, and post-transition
  actions through one coherent vocabulary with no sentinel fake rules.
- Automatic profile selection depends only on source state; output refresh is
  available only as a completed-transition event fact.
- Every manual and automatic transition has deterministic precedence,
  persistence, logs/OSD diagnostics, and a safe renderer-rebuild boundary.
- Refresh-triggered actions have explicitly documented trigger points,
  rate semantics, delay, deduplication, cancellation, and failure behavior.
- Existing supported configuration continues to work unchanged during the
  defined compatibility period, with clear migration diagnostics.
- `VideoProcessorRenderer.cfg` and its canonical documentation agree and the
  examples are parser-validated.
