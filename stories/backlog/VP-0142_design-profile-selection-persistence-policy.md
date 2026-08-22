# VP-0142: Design profile-selection persistence policy

## Status

Backlog (2026-08-22). Target-model VP Renderer profiles currently persist
their manual selections unconditionally so the first real state change creates
and saves `VideoProcessor.state`. Define the durable, user-configurable policy
before introducing configuration switches or changing that safe default.

## User story

As a VideoProcessor user, I want a clear and predictable persistence policy
for manual profile selections, so restarts restore the settings I expect
without silently retaining state that should be session-only.

## Confirmed problem

The VP-0079 target configuration model originally forced profile persistence
off, ignoring the existing `persist_profile_selection` concept. That prevented
the runtime from creating the adjacent `VideoProcessor.state` file even after
a real F5/F6-style manual selection. The immediate repair makes target-model
selections durable. The eventual configuration contract has not been designed:
there is no agreed default, user-facing scope, per-profile-group behavior, or
migration rule for existing installations.

## Required behavior

1. Define whether persistence is enabled globally, per profile group, or both;
   include display, viewport, input, scaling, output, queue, and LLDV groups.
2. Define a stable configuration syntax, defaults, migration behavior, Config
   UI presentation, and generated documentation.
3. Preserve the current durable behavior until a replacement policy is
   implemented and accepted; no configuration migration may silently discard a
   user’s existing `VideoProcessor.state` selections.
4. Specify reset-to-automatic behavior, invalid or removed profile handling,
   first-run state-file creation, atomic writes, and coexistence with display
   recovery records in the same state file.
5. Ensure all manual profile changes either persist successfully or report a
   clear actionable failure; automatic source-driven selections must follow the
   documented policy.

## Acceptance criteria

1. The approved policy identifies every profile group and its persistence
   default, override scope, and reset behavior.
2. A configuration schema and Config UI design reject invalid values and make
   the effective policy discoverable without examining source code.
3. A migration plan preserves valid saved selections and gives users a clear
   diagnostic for stale or invalid entries.
4. Tests cover enabled and disabled persistence, group overrides, first-change
   state-file creation, restart restoration, reset-to-automatic, and write
   failure without corrupting unrelated recovery records.
5. The configuration reference and release notes explain the final behavior.

## Boundaries and related work

- The immediate target-model repair is intentionally unconditional and is not
  replaced by this design story until the new contract is implemented.
- Do not redesign F5/F6 profile meanings, external batch-state markers, or
  renderer output policy as part of this story.
- Coordinate with VP-0079 (canonical profile configuration) and VP-0141
  (live renderer-settings application) before changing persisted selection
  semantics.

## Likely implementation areas

- `src/VideoProcessor-Lib/RendererProfileConfig.h`
- `src/VideoProcessor-Lib/UnifiedProfileRuntime.cpp`
- configuration schema, Config UI, configuration reference, and regression
  tests for persisted state
