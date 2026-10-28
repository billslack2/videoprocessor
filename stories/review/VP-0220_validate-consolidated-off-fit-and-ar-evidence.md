# VP-0220: Validate consolidated Off fit and AR evidence changes

## Status

Review

2026-10-28: Consolidated candidate is open in [PR #223](https://github.com/billslack2/videoprocessor/pull/223), replacing PR #203 and PR #213 as the review/testing vehicle. Merge is explicitly not approved. Automated qualification passed; user playback acceptance and a deliberate keep/change/drop decision remain pending. This story records already completed local integration and testing, rather than starting an unrelated implementation.

## Source and deployment

- Repository: `billslack2/videoprocessor`.
- Current beta/default: `v1.4.02-beta`, pinned base `74c67da75afb12d09691850e1fb9b02d31b70a06`.
- Branch: `codex/vp-0220-off-ar-consolidation`.
- Exact tested and deployed head: `e132c6d8483c91a457a983e0ec4e6caee2ac4773`.
- PR worktree: `E:\codex\videoprocessor\vp-0220-off-ar-consolidation`; identical commit to the preserved build worktree `E:\codex\videoprocessor\rc6-integration-20261028`.
- Test installation: `C:\Videoprocessor\vp-test-rc6-20261028`; launch `Start-RC6-Test.cmd` there.
- Previous RC6-only test installation backup: `C:\Videoprocessor\vp-test-rc6-20261028-backup-before-pr203-pr213`.
- Host, both renderer variants, Config and discovery DLL match the completed x64 Release build hashes. Test configuration/state were preserved. Normal `C:\Videoprocessor\vp` installation was unchanged. Playback was not launched as part of this deployment.

## Problem and retained scope

Off mode can alternate between a current measured envelope and an older submitted envelope, changing apparent picture size while visible bar content is unchanged. AR evidence can lose a valid confirmation run as brightness changes switch the preferred evidence route. The candidate combines only the reviewed changes with current beta.

1. From PR #203 (`697c1bcd`): independent source-owned Off fit, per-edge hold expiry, unknown/duplicate/stale-source handling, and preservation of visible content in any encoded bar. Native picture detection remains separate from presentation accommodation. Active Off accommodation suppresses optional cropping and uses linear fit even with NLS selected; released native views resume ordinary policy.
2. From PR #213 (`ddf5d666`): independently complete AR proof runs, bounded uniform raised-matte qualification, and native queued-proof timing/parity. No inference of native aspect from the physical screen or a preferred standard ratio.
3. RC6 corrections: five-binary build-receipt validation, PowerShell UTF-8 handling, public release asset documentation, and regression expectations for existing two-row attached backing.
4. Preserve newer beta provenance safeguards, Classic bar-content handling, and NLS readiness reuse. Keep both the existing provenance tests and new Off tests. Reconcile PR #213's sentinel-backed overlap checks with RC6's independent CleanupOnly boundary check; do not restore the shortcut that disabled attached backing.

Excluded: PR #200, DV trim diagnostics, unrelated local branches, and exploratory variable-gradient/dark-owner/full-raster AR research. VP-0218 retains that separate unfinished research scope.

## Supersession assessment

Missing symbols alone were not treated as proof that an old feature was still needed. Reviewed later beta changes and exercised the control replay with current production recovery functions. `RecoveryReplayDemonstratesCompetingSubmittedEnvelopeTracks` still reproduces both competing envelopes, while `OffSourceOwnerStopsDelayedOutputFromAlternatingFinalFit` stabilizes them. No complete newer replacement for either PR's production behavior was found. This evidence supports testing the candidate, not automatic merge approval or a universal no-regression claim.

The old PRs are replaced as review vehicles. VP-0213 is superseded by this story; the contained PR #213 changes are tested here while the broader VP-0218 research remains separate.

## Automated evidence

[Evidence folder](../assets/VP-0220/consolidation-20261028/) contains the deployment hashes, semantic review and test receipts.

- Clean x64 Release solution rebuild passed. An earlier incremental attempt failed with corrupt compiler debug information (LNK1103); the clean rebuild regenerated intermediates. Both logs are retained.
- 491 affected crop-policy, Off-mode, AR evidence and lookahead tests passed.
- Required standard and beta renderer GPU/cue suites passed: 208 each.
- The two reconciled attached-backing/cleanup tests passed separately on both dependencies: 4 additional executions.
- Total: 911 passing test executions. No assertion was weakened to obtain a pass.
- RC6 packaging support/identity checks passed earlier: 59 + 12; both changed scripts parsed. Full installer lifecycle/export was not repeated for this consolidated playback candidate.

## Playback acceptance plan

Record clip/source, renderer dependency, input raster/range/frame rate, selected profiles, NLS state, hold setting, log timestamps, observed source/target bounds and pass/fail for every run. Compare with current beta using the same settings. The 2.35:1 CIH screen is display context, not the desired detected aspect. Inspect active configuration/state and the test installation's own logs before interpreting results.

- [ ] Off: stable fit while subtitles, menus, scrub controls or other graphics remain visible; test bottom, top, side and simultaneous bar content. No alternating zoom, clipping, subtitle translation, reconstruction or repainting.
- [ ] Hold: zero releases on the next reliable negative; positive hold bridges brief gaps and expires once. Fresh content renews only the appropriate extent; unknown/duplicate/repeated outputs cannot renew or resurrect stale ownership. Include hold 0 and 2000 ms and a configured fractional interval.
- [ ] Real AR changes: scope-to-taller and reverse transitions, brightness changes and uniform raised mattes. Measure boundaries from source pixels; check false crops, missed bars, caption/picture preservation, timing and stability. Do not claim the unresolved variable-gradient YouTube TV ad is fixed by this candidate.
- [ ] Ownership transitions: Off, Classic and reconstruction switches; startup, pause/resume, queued/repeated frames, Smooth Motion and moving-picture handoffs. Existing provenance and caption protections must remain effective.
- [ ] NLS/NLS+: preserve warmed engagement behavior. Confirm linear fit while an Off content envelope is active and normal policy after release, without stutter or competing geometry.
- [ ] Backing/cleanup: inspect top and bottom panels, rounded edges and scaling seams. Attached display backing may overlap two rows into the bar; source cleanup must stop at the measured picture boundary without erasing live corners.
- [ ] Performance: inspect measurement timing because Off measures every eligible source and differing black references can prevent reuse of an earlier scan. Compare frame timing, drops/repeats and responsiveness against beta; do not infer live performance from policy tests.
- [ ] Repeat relevant playback checks with both shipped renderer dependencies and record failures and limits explicitly.

## Decision and completion

After playback, record which retained parts are accepted, need adjustment or should be removed because beta has a better replacement. Any source changes need appropriate fresh build/tests and a separately identified test deployment. Keep PR #223 open and unmerged until the user explicitly approves a merge. Close this story only after the user accepts the final disposition, with exact source/PR and playback evidence recorded. Do not merge merely because automated tests passed.
