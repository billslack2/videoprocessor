# VP-0169: Persist GUI edits into missing configuration sections

## Status

Done (2026-09-01). The reported LLDV configuration persistence defect was
confirmed: Config's `SetKnown` write path only added a key when the destination
section already existed, so entering LLDV metadata in a configuration without
`[lldv]` did not save the values. The same write contract could affect other
optional GUI-managed sections.

Commit `1d5c731ba663df01bb9148cdaae3436afe358463` changes `SetKnown` to create
the destination section before writing its first key. This fixes `[lldv]` and
provides the same protection for all current and future GUI controls that use
the editor's normal configuration writer. The branch `v1.3.005-beta` was
created from the verified `v1.3.004-beta` tip, pushed directly, and promoted
to the repository default branch at the user's request.

The x64 Release `VideoProcessor-ConfigTests` build passed. The new focused
regression test removes `[lldv]`, `[logging]`, and `[shortcuts]`, edits all
four LLDV fields plus a logging and shortcut control through the GUI, saves,
and verifies every missing section and value was persisted. The existing LLDV
singleton migration test also passed. No deployment or active configuration
file was changed.

## User story

As a VideoProcessor operator, I want Config to create an optional
configuration section when I edit one of its fields, so newly exposed settings
such as LLDV metadata reliably persist in minimal or older configuration files.

## Acceptance criteria

- Editing any LLDV metadata field creates `[lldv]` when it is absent and
  preserves all entered values on save.
- The normal Config writer creates a missing optional destination section,
  rather than silently discarding the edit.
- Regression coverage proves the behavior for LLDV text fields, logging
  checkboxes, and shortcut fields.
- The x64 Release Config test project builds and the focused regressions pass.

## Related stories

- VP-0097: Safe standalone configuration editor and VP integration.
- VP-0164: Backdate fully proven inward lookahead transitions (the prior
  default beta branch).
