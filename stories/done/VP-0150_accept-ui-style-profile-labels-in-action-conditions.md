# VP-0150: Accept UI-style profile labels in action conditions

## Status

Done — accepted and deployed on 2026-08-26; rebased, validated, and merged
into the beta integration branch on 2026-08-27.

Confirmed source baseline: `origin/v1.3.001-beta` at
`56a9c73270c833404817e292a6c35bb165a05a72`. The default integration branch
was confirmed as `v1.3.001-beta`.

Implementation branch: `codex/ui-queue-action-alias`.
Source commit: `e0d302e` (`fix: accept UI profile labels in actions`), pushed
to `origin/codex/ui-queue-action-alias` and fast-forward merged into
`origin/v1.3.001-beta`.

## User story

As a VideoProcessor configuration user, I want an action condition to accept
the profile name shown in the UI as well as its stable configuration
identifier, so a rule for the visible **Low Latency** queue profile works
without requiring users to discover the internal `low_latency` spelling.

## Scope

- Action conditions for `${profile.<group>}` and
  `${previous_profile.<group>}` accept either an underscored stable identifier
  or its UI-style spaced spelling, case-insensitively.
- The action argument expansion contract remains stable: `${profile.queue}`
  still passes `low_latency` to an external program.
- The same comparison behavior applies to all profile groups, not only queue
  profiles, without changing source-variable or keyboard-shortcut matching.
- Document the two valid queue-condition spellings in `CONFIGURATION.html`.

## Acceptance criteria

1. Both `${profile.queue}=="low_latency"` and
   `${profile.queue}=="Low Latency"` match the selected `low_latency` queue
   profile.
2. `${previous_profile.queue}=="Low Latency"` matches when leaving that
   profile.
3. Action command arguments retain the stable value `low_latency`.
4. Non-profile expression comparisons and case-sensitive `${key}` matching
   retain their existing behavior.
5. The configuration reference describes the UI-style condition alias and
   stable action-argument output.

## Implementation evidence

- `DisplayRuleExpression` now normalizes profile condition operands by mapping
  UI spaces (and hyphens) to the stable underscore separator.
- Regression coverage exercises canonical and UI-style current queue matching,
  UI-style previous-queue matching, and confirms argument expansion yields
  `low_latency`.

## Validation and deployment evidence

- x64 Release builds succeeded for `VideoProcessor-GUI`,
  `VideoProcessor-VPRenderer`, and the native test project.
- Targeted native test
  `UnifiedActionsPublishCommittedSourceAndProfileEvents` passed.
- After rebasing onto the updated beta tip, a clean x64 Release native-test
  rebuild and the same targeted test passed again before the merge.
- The release executable and matching VP Renderer plugin DLL were deployed as
  one versioned pair to `C:\Videoprocessor\vp`; their installed SHA-256 hashes
  matched the respective build outputs.
- Deployment initially exposed an API mismatch between the new executable and
  a pre-existing plugin DLL. The matching API-13 plugin from this same Release
  build was then deployed, restoring VP Renderer discovery and the `Shift+A`
  renderer shortcut. The active `VideoProcessor.cfg` was not modified.
