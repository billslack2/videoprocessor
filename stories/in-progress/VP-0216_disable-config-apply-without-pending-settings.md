# VP-0216: Disable Config Apply when no settings are pending

## Status
In Progress

## Request
After Apply successfully saves settings, disable the button until settings change again. User explicitly prioritizes this conventional behavior over using clean Apply to reload externally edited calibration LUTs.

## Implementation
Clean current-beta worktree E:\codex\videoprocessor\config-apply-dirty-state-20261026, branch codex/config-apply-dirty-state-20261026, baseline 81ac960fbd62619374fba1c16630b8feac364d59. Preserve existing local deployments and active configuration. No deployment requested for this correction.

## Acceptance
- Clean local and remote documents have Apply disabled; a valid edit enables it.
- Successful local and remote saves disable Apply; OK remains available to close.
- Reverting an edit disables Apply again. Invalid changes cannot apply; failed saves retain pending changes.
- Missing configurations remain creatable. Clean Apply/Ctrl+S cannot rewrite files or trigger local notifications or remote Apply RPCs for external LUT edits.
- Update LUT help and targeted save/validation/remote regression tests; build x64 Release.

## Tracker audit
Fetched origin/main; 234 canonical IDs match 234 table rows with no duplicates or missing IDs. Maximum root 215 agrees with registry; assigned next root 216.
