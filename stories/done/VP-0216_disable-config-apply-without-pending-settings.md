# VP-0216: Disable Config Apply when no settings are pending

## Status
Done

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

## Combined local deployment - 2026-10-26

User requested deployment including earlier changes. Verified installed Config editor EF86DD1D... against its prior bf44dbee deployment receipt, and the runtime against subtitle/vsync e8bc0638. Clean integration branch codex/deploy-config-apply-combined-20261026 contains both exact ancestors, the Apply fix, and an inventory documentation label correction. Final clean source b3b36c1d69739c6f046092ae332f6e8914cdb2a1 is pushed and unmerged. Existing feature branches and worktrees were preserved. This retains the earlier Scaling/Tone Mapping, charts, dock/layout and migration work when replacing the editor, as well as subtitle alignment and vsync runtime changes.

Full solution x64 Release build passed. All 979 selected native subtitle/GPU, display/timing/cadence, configuration/migration and rendering checks passed. Config integration: 22 passing cases, including all 13 Apply-focused cases; one retained docking/Advanced-focus test fails identically on the prior qualified bf44dbee binary in a control run. It is a pre-existing limitation, not a passing test or a newly demonstrated regression. Offscreen responsive-layout failure passed on the real Windows Qt platform. Native inventory test exposed a missing AUTO-behavior label in the retained dithering reference; corrected the documentation and reran all selected native checks successfully. No docking product code or assertion was weakened to manufacture a pass. Native UI inspection helper failed to initialize; live button inspection was unavailable. Logs and failing/control receipts remain at E:\codex\diagnostics\deploy-config-apply-combined-20261026.

Deployed matching host, standard renderer, beta renderer, Config editor and discovery DLL with runtime records and host/renderer symbols: 13 installed files, all verified against Release artifacts. Twenty-three existing dependencies match and were retained. Backup: C:\Videoprocessor\vp\backup-before-combined-apply-b3b36c1d-20261026-092835-756. The Config editor was hidden in the tray, stopped for replacement and restored to the tray with the updated binary; PID 13936 remained responsive. Player stayed closed. Active VideoProcessor.cfg and VideoProcessor.state were backed up and remained byte-identical; configuration edits: none. The captured CFG hash reflects the user's most recent saved settings rather than an older deployment's settings.

[Build/source/test identity](../assets/VP-0216/combined-deployment-20261026/build-identity-final.json), [deployment receipt and hashes](../assets/VP-0216/combined-deployment-20261026/deployment-receipt.json), [reviewed Config results including known failure](../assets/VP-0216/combined-deployment-20261026/config-results-reviewed.json). PR #206 is updated. Story remains Review pending user acceptance; no beta merge or public release.

## Three-branch merge complete - 2026-10-26

User explicitly authorized merging all three deployment inputs. [Atomic integration PR #207](https://github.com/billslack2/videoprocessor/pull/207) merged into current beta v1.4.02-beta as 87f61f77d3d95ddbfc1eeae343501d763d49e80b, from pinned base 81ac960f and clean integration head 644571d461061eca5c27a5e973244fc26afc2ca0. Both latest-beta/default discovery and PR mergeability/check-state verification passed; no CI checks were configured. The release skill's pinned Prepare/Merge workflow was used. No unrelated pending PR was merged.

Verified all three exact branch heads are ancestors of beta: subtitle/vsync e8bc0638, retained Config/Scaling/Tone Mapping bf44dbee, and Apply 09f9f382. Original PRs #205 and #206 are now MERGED. The integration additionally records original Apply ancestry (the deployed branch had cherry-picked it), changing no files. Beta tree 90a850d37d63cf1160ba6e260361e7b092fbd876 equals the successfully built/tested/deployed b3b36c1d tree exactly. Existing validation remains 979 selected native passes, 22 Config passes including all 13 Apply cases, and one documented pre-existing docking/Advanced-focus failure reproduced with the prior binary. No source behavior changed during this merge, so those exact-tree results were reused.

[Verified merge receipt](../assets/VP-0216/three-branch-merge-20261026/merge-receipt.json). Apply work is Done following the requested deployment and explicit merge authorization. Installed binaries remain the matching tested b3b36c1d build; no player restart, additional deployment or configuration edit was needed. The broader VP-0207 incident and existing VP-0215/VP-0211 validation limits remain tracked separately.
