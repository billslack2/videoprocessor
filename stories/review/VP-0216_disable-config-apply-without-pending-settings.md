# VP-0216: Disable Config Apply when no settings are pending

## Status
Review

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

## Implementation and verification - 2026-10-26

Clean source 09f9f38217af07dd4ac781be388bbe54aed76469, based on beta 81ac960f. [Draft PR #206](https://github.com/billslack2/videoprocessor/pull/206), unmerged. Clean local/remote Apply is inactive, successful saves disable it, valid pending edits enable it, and reverting an edit disables it. Clean invocations no longer request external LUT reloads; OK still closes clean documents. Save failures retain pending settings and retry availability. Updated LUT guidance describes applying settings changes or restarting the renderer.

Config editor and test targets built successfully as x64 Release, with matching runtime-manifest hashes. Thirteen focused Qt integration cases passed: Apply/OK/Cancel; clean no-op local Apply; dirty-to-clean remote RPC; locked-file save failure/retry; missing-file creation; invalid renderer recovery and actual saved-renderer reversion; shortcut edit undo; ordinary edits; legacy visibility; LUT discovery; calibration; General/backend overrides; subtitle save compatibility. This is targeted coverage, not a complete UI/native suite.

The first validation fixture incorrectly treated selecting VP Renderer as reverting the deployed fixture's saved madVR renderer; corrected it to assert both a different valid selection and restoration of the actual saved value. Preserved the failed receipt and successful corrected rerun. An initial standalone test output-path mistake and missing SDK argument for the production discovery dependency were corrected; no production behavior changes were inferred from those setup failures. Full logs and receipts: E:\codex\diagnostics\config-apply-dirty-state-20261026.

[Verification and artifact identity](../assets/VP-0216/verification.json). [Focused case results](../assets/VP-0216/test-results.json). No deployment, active configuration edit or player restart. Existing local subtitle/vsync runtime remains installed. Ready for code review and requested deployment.
