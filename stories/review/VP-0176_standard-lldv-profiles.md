# VP-0176: Standard LLDV profiles

## Status
Review

Implementing on codex/lldv-standard-profiles in E:\codex\videoprocessor\lldv-standard-profiles, based on freshly fetched origin/v1.3.005-beta at ee9b3f73. The user's standing instructions authorize the discovered latest beta base.

## Request
Make LLDV use the same standard profiles as the other configuration pages. Migrate existing singleton settings into one profile without changing their values.

## Readiness review
The current runtime already resolves ordered LLDV profiles and applies their metadata to both renderer paths. The shared profile editor already supports LLDV fields, defaults, ordering, shortcuts and rules. Reuse it and remove the reverse singleton migration. Alternate detection remains global and restart-required. No renderer resource or pipeline changes are needed. A clean current-beta worktree is ready.

## Acceptance criteria
- LLDV offers the standard profile list, naming, ordering, add/remove, shortcuts, rules and active state.
- Existing singleton settings migrate into one named profile, retaining values and comments; existing named profiles retain their names.
- Multiple profiles save/reload independently and resolve through the existing runtime.
- Alternate detection remains global and existing LLDV-aware input migration is retained.
- Relevant x64 Release editor tests pass; documentation describes profiles.

## Progress
2026-09-08: Audited tracker: 194 canonical files and 194 index rows, no missing or duplicate IDs; maximum root VP-0175. Inspected runtime and shared editor support.
## Review evidence
Implementation commit: 971b6fad. Draft PR: https://github.com/billslack2/videoprocessor/pull/84 against v1.3.005-beta.

The shared editor now supplies LLDV list management, naming, ordering, rules, shortcuts, cycling and active status. Legacy [lldv] migrates on save to one named profile; named sections are preserved. The runtime is unchanged. Alternate detection remains global. Sample configuration and reference documentation are updated.

Validation: x64 Release configuration app and test executable built successfully. Final focused tests passed: LLDV standard profiles (including migration values/comments, lifecycle, rules, cycle shortcut persistence, and runtime metadata resolution), named metadata preservation, optional-section creation, and every-page round trips. Full suite: 63 passes and one window-topmost timing failure; the failing stable-reveal test passed in isolation. Existing compiler shadowing and Qt deployment-environment warnings remain.

Remaining: code review and user acceptance before merge/release. No deployed binaries or active user configuration were changed.