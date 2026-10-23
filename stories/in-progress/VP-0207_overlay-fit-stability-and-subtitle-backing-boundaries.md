# VP-0207: Overlay fit stability and subtitle backing boundaries

## Status
In Progress

User accepted the improved live result and authorized merging on 2026-10-23. PR #195 merged into v1.4.02-beta as 73acf650739401cdfdfccf2e111290b3265299e2. The merged tree exactly matches tested feature 6608b736.

Implementation on `codex/subtitle-overlay-stability-20261023`, starting at current beta `dfd34ea6f6365eddb17d986914bb7792836d694b`, in `E:\codex\videoprocessor\subtitle-overlay-stability-20261023`.

## Report
Paused subtitle toggle causes repeated scope/full-raster flashing; playback scrub controls should remain visible in a stable zoomed-out presentation. Subtitle backing lips remain intermittently visible. Screenshot supplied 2026-10-23.

## Evidence and work
Deployed log identifies host 4d1d62d0. Current logs repeatedly reject finite backing with `lip-top-closure-occluded`, `lip-outline-boundary-occluded`, and `lip-black-span-does-not-cover-glyphs`. Shared vertical policy explicitly lets a one-edge translate replace held FIT.

## Acceptance
- Hold a zoomed-out FIT across changing overlay classification for the existing bounded interval; allow immediate outward growth and expire normally.
- Verify finite subtitle backing using glyph-bounded width and a bounded collar above its edge, without extending cleanup into scene shadows.
- Preserve frame/cue ownership, sample budget, native picture transitions and existing cleanup exclusions.
- Pass x64 Release regression tests. Live validation remains required before claiming the reported playback incident resolved.

## Implementation and validation — 2026-10-23

- Draft PR: https://github.com/billslack2/videoprocessor/pull/195
- Source: `5ab30a23f7e463ab520d65b1e6a5e71d87437ebb` (preceded by implementation commit `bcb631297a7b75d7e4f51f4e04757d0fd43cbc80`).
- Clean x64 Release solution Rebuild succeeded with the official installed Magewell SDK. Both generated host/renderer identities report the final source commit and `dirty=false`.
- Full native suite: **3,153 passed, zero failures**, 13.4128 minutes. Report: `E:\codex\videoprocessor\subtitle-overlay-stability-20261023\TestResults\overlay-backing-clean-full.trx`; build/test logs in that worktree are `build-clean-release.log` and `test-clean-full.log`.
- Displayed pending translation/FIT envelopes are remembered only after successful submission with matching existing native admission/context; no new native aspect authority is granted.
- Held zoomed-out FIT survives fresh one-edge classification, grows immediately for new outward occupancy, and uses the existing bounded release and extent expiry.
- Backing closure can be corroborated in a bounded picture collar (up to half a glyph height, capped at 32 native pixels). The glyph-bounded side search, exhaustive black body, ownership and sample budget remain enforced. Rounded corners above glyphs may narrow inside the lower text's projection. Scene collar pixels never enter cleanup.
- Native regression coverage includes P010/P210/decoded DV/RGB, both bars, attached shadows, rounded corners, invalid ownership/context, budget exhaustion, failed/late submissions and bounded overlay release. Existing actual GPU cleanup/layer tests passed.

## Remaining review

Not merged. Deployed for user testing on 2026-10-23 as recorded below. Replay the user's paused subtitle toggle and playback time-bar interaction; live incident resolution is not yet claimed.

## Test deployment — 2026-10-23

User authorized: deploy it to test. Deployed clean x64 Release commit `5ab30a23f7e463ab520d65b1e6a5e71d87437ebb` to `C:\Videoprocessor\vp`. Backed up and replaced the host, standard renderer, and beta renderer together. Verified all three deployed SHA-256 values against their build runtime records. Configuration hashes remained unchanged before and after launch; no configuration edits.

Backup: `C:\Videoprocessor\vp\deployment-backups\vp0207-5ab30a23-20261023-145959`. Host launched with working directory `C:\Videoprocessor\vp` as PID 22084. Fresh runtime logs confirm host and renderer both use the intended commit with `dirty=0` and the configured beta renderer was selected. Receipt and startup evidence: `stories/assets/VP-0207/deployment-5ab30a23-20261023`. Story remains Review pending the user's live test.

## Mode clarification — 2026-10-23

User confirmed flashing improved. Off must zoom out only to the bounded union of rendered picture and overlay content, allowing black bars on every side; it must not require a stable Classic movement target or default to full raster. Classic moves the picture. Existing advanced reconstruction behavior is accepted and must remain unchanged. Continuing implementation on the same clean feature lineage; current remote beta remains dfd34ea6.

## Off mode correction and test deployment — 2026-10-23

- Final source: `81cc98532af1b3709bb02afdee68bb16738557ab`, pushed to draft PR #195. Converts a measured Off overlay TRANSLATE proposal to FIT before Classic translation confirmation; changing scrub targets no longer reset Classic's movement vote while Off is selected. Shared FIT confirmation and bounded envelope composition remain in force.
- Classic and advanced styles retain the exact existing translation-routing path. No advanced reconstruction shader or settings changes.
- Clean x64 Release solution Rebuild passed at the clean final commit. All **450 focused regressions passed**, including Off target changes, unchanged Classic motion confirmation, bounded four-edge output with black bars on all sides, subtitle handoff/backing and advanced retention. Report: `E:\codex\videoprocessor\subtitle-overlay-stability-20261023\TestResults\off-fit-clean-targeted.trx`. Build/test logs: `build-off-fit-clean.log`, `test-off-fit-clean.log`. Earlier full 3,153-test coverage belongs to commit 5ab30a23, not a claimed full rerun at 81cc9853.
- Updated the ongoing authorized test deployment: host and standard/beta renderers backed up together, replaced from the clean final Release build, and verified against build-record SHA-256 values. VP shut down gracefully and relaunched as PID 71692. Startup confirms clean host and renderer commit 81cc9853 and beta renderer selection.
- Backup: `C:\Videoprocessor\vp\deployment-backups\vp0207-off-81cc9853-20261023-151454`. Configuration hashes unchanged before/after launch; no configuration edits. Receipt and startup evidence: `stories/assets/VP-0207/deployment-81cc9853-20261023`.
- User confirmed flashing improvement and accepted existing advanced reconstruction behavior. Remains Review pending live confirmation of clarified Off bounded fitting. Not merged.

## Clipped time-bar follow-up — 2026-10-23

User reports scrub line/time labels still clipped while its preview thumbnail is visible; 16:9 comparison exposes the complete controls. User requests Off retain the bounded zoom-out for about two seconds, configurable in the UI as Hold zoom for. Advanced reconstruction remains accepted.

Deployed 81cc9853 log, 15:22:25–26, sees lower overlay rows spanning 1806..2068 with peak 1760 samples. The bounding depth flips overlay_bottom from true to false although the UI is a thin line plus separated labels. This disqualifies current UI FIT ownership. Investigating actual occupied-row density rather than treating intervening black as picture. Current beta remains dfd34ea6.

## Scrub-control footprint and Off zoom hold — 2026-10-23

- Source `6168c11e57d1aa8236fde53362c3d4cd9e24e5a3`, pushed to draft PR #195. Off now uses actual occupied-row support when classifying a bar overlay; black gaps between the scrub line and labels no longer make that sparse UI appear to be continuous picture fill. Its complete bounding rectangle is still retained for rendering. Classic/advanced classification is unchanged.
- Pixel regression uses a 3840x2160 native P010 frame with picture 384..1776, wide line 1806..1826 and separated labels 2046..2068. Confirms 42 occupied rows across a 262-row bounding span, successful independent UI FIT ownership, full bounded extent, cache preservation and two-second release hold. Mirrored top-bar and continuous-picture negative cases are included.
- Off profile control **Hold zoom for** (`off_zoom_hold_seconds`), default 2 seconds, accepts 0..30 seconds independently of Classic's timer. Runtime reads the selected Off value, and existing hold/extent lifetimes use it. UI control appears only for Off.
- Clean x64 Release solution Rebuild passed. **670 focused native tests passed**, plus Qt **Off zoom hold round trip** and existing **subtitle profile tab round trip** passed. Tests cover default/range/schema, independent timers, hidden controls in other styles, comment preservation, save/reopen and prior subtitle/aspect behavior. Logs: `build-timebar-hold-clean.log`, `test-timebar-hold-native.log`, `test-timebar-hold-ui.log`, `test-timebar-subtitle-ui.log` in the source worktree. Native TRX: `TestResults/timebar-hold-native.trx`. New UI visually verified using an isolated fixture; screenshot is fixture UI, not user configuration.
- Deployed the clean Release host, standard/beta renderers, Config executable and Config discovery DLL together; all five deployed hashes match their build runtime records. Backup: `C:\Videoprocessor\vp\deployment-backups\vp0207-timebar-6168c11e-20261023-153901`. Configuration hashes unchanged; no active settings edits. VP relaunched as PID 78088, and fresh host/renderer logs both confirm commit 6168c11e with dirty=0.
- Evidence: `stories/assets/VP-0207/deployment-6168c11e-20261023` includes receipt, startup identity, pre-fix raw detection lines and the isolated UI screenshot. No captured movie pixels committed.
- Remains Review for the user's paused scrub-bar replay; live incident resolution is not yet claimed. Not merged. Earlier full 3,153-test validation remains specific to 5ab30a23.

## Off settings clarification — 2026-10-23

User clarifies Off must detect rendered content in black bars without glyph or script hints. Hide the script-hint control for Off while preserving its value for subtitle-processing styles; verify the Off content path is independent of glyph detection. Continuing the authorized test deployment iteration.

## Off content-only settings and deployment — 2026-10-23

- Source b099cd8db071d44f69bd8942cfe6f7d2a9ccb59e, pushed to draft PR #195. Off hides the entire Subtitle script hint row while retaining Hold zoom for. Switching to Classic or either advanced style restores the saved hint. Hidden values survive save/reopen; no active configuration edits were needed.
- Verified the existing Off path measures luma content and occupied rows in black bars, independently of glyph or script hints. Runtime detection and advanced reconstruction are unchanged by this UI correction.
- x64 Release solution Build passed at the clean final commit. Qt Off zoom hold round trip and subtitle profile tab round trip both passed. The first run caught an incorrect form-row target; corrected it before the final passing run. Final logs: build-off-content-only-final.log, test-off-content-ui-final.log and test-off-content-subtitle-ui-final.log in the source worktree. Visually verified Off using a separate fixture. Earlier 670 native regressions apply to 6168c11e; no native implementation changed in this follow-up.
- Updated the ongoing authorized test deployment with matching host, standard/beta renderers, Config executable and Config discovery DLL. All five SHA-256 hashes match Release x64 build records. Backup: C:\Videoprocessor\vp\deployment-backups\vp0207-off-content-b099cd8d-20261023-154704. Configuration hashes unchanged after launch; configuration edits: none. Runtime startup confirms host and renderer both b099cd8d with dirty=0; beta renderer selected, PID 60072.
- Receipt, startup identity and isolated UI screenshot: stories/assets/VP-0207/deployment-b099cd8d-20261023. No movie pixels included. Remains Review for the user's live paused control-bar test. Not merged.
## Off navigation fit still missing — 2026-10-23

User reports Off still fails to zoom out for visible navigation/time controls. Runtime b099cd8d detects bottom content at 1806..2068, including 190 occupied rows, but current UI ownership still requires overlay-like shape. Investigate the complete measurement, confirmation, recovery and submitted presentation path; Off must accommodate measured bar content independently of glyph or UI shape classification.

## Measured Off bar-content fit — 2026-10-23

- Final source 6608b736e93a0f207ce2a9f451e41d830d9fcc50, pushed to draft PR #195. Previous UI-only deployment did not satisfy the user's live navigation-bar test. The new runtime correction removes three remaining gates: compact overlay-shape ownership in Off, repeated FIT confirmation before accepting current bar pixels, and composition of provisional coarse AR edges into the measured vertical fit. Logged content 1806..2068 spans 190 occupied rows when the preview is included; raw FIT was also lost when a coarse top edge at 382 differed from native top 384.
- Off now accepts a current bar-content FIT immediately. A dedicated CURRENT_BAR_CONTENT proof requires the current source identity, trusted native base, measured occupied expanding edges and the exact measured envelope plus padding. Glyph and overlay-shape flags are not required. Measured Off vertical extents supersede coarse AR history; raw provisional geometry cannot force unrelated full-raster expansion. Existing hold and submitted-envelope retention remain in force. Classic and advanced shape/confirmation paths are unchanged.
- Native P010 regression includes 42, 190 and 262 occupied-row cases over a 262-row span, on both bars, and checks complete padded extent, immediate Off versus repeated Classic confirmation, release hold and cache behavior. Composition cases include two-pixel coarse noise and full-raster placeholders. A further regression checks normal/near-black recovery, final aspect-fill exclusion, admission and submitted envelope, plus rejection of stale, unsupported or oversized proofs.
- Clean x64 Release solution Rebuild passed at the final commit. All **695 focused native tests passed**, including the complete retained-scope recovery suite, plus Qt Off zoom hold round trip and subtitle profile tab round trip. Logs: build-off-measured-envelope-clean.log, test-off-measured-envelope.log, test-off-measured-ui.log, test-off-measured-subtitle-ui.log in the source worktree. Native TRX: TestResults/off-measured-envelope.trx. The earlier build caught a test-helper name error; fixed before final validation. No full native suite rerun is claimed at this revision.
- Updated the authorized test deployment with the matching host and standard/beta renderers only; all three hashes match Release x64 build records. Preserved the existing b099cd8d Config binaries and open editor session because this is a runtime-only correction. Active configuration edits: none; hashes unchanged after launch. Backup: C:\Videoprocessor\vp\deployment-backups\vp0207-off-content-6608b736-20261023-160222. Startup confirms host and renderer 6608b736, dirty=0, beta selected, PID 11436.
- Evidence: stories/assets/VP-0207/deployment-6608b736-20261023. Remains Review for the user's live Off navigation-bar replay. Not merged; do not treat the previous failed replay or synthetic validation as live acceptance.
## Acceptance and merge — 2026-10-23

User: "yes that looks better so lets merge this." Marked PR #195 ready and merged using the vp-release pinned-head/base procedure. Reviewed source 6608b736e93a0f207ce2a9f451e41d830d9fcc50, integration base dfd34ea6f6365eddb17d986914bb7792836d694b; beta had not advanced. GitHub reported clean mergeability and no pending/failing checks. Final clean Release validation remains 695 native tests and both Qt settings round-trip tests passed.

Merged: https://github.com/billslack2/videoprocessor/pull/195 — commit 73acf650739401cdfdfccf2e111290b3265299e2 on v1.4.02-beta. Fetched the exact beta ref, verified merge ancestry and identical Git trees between the merge and tested feature. No new combination required retesting. Merge evidence: stories/assets/VP-0207/merge-73acf650-20261023/merge-result.json.

The accepted tested deployment remains the clean Release source 6608b736 with recorded matching host/renderer hashes and unchanged configuration. This merge did not require another deployment or release package. Story complete.
## Subtitle backing flash reopened — 2026-10-23

User reports intermittent subtitle occlusion flashing during Ben-Hur after accepting the Off navigation fit. Reopened the unresolved backing portion; the merged Off improvement remains accepted. Preserved runtime 6608b736 logs under E:\codex\diagnostics\vp0207-backing-flash-20261023-1632. At 16:31:21–22, same cue 12 frames 1653 and 1669 have valid finite backing proof but application rejects physical 383..1777 or 384..1777 against composed 384..1776; neighboring frames apply cleanup. Frames 1659/1660/1668 also reject acquisition at the shifted edge. Current beta is 73acf650. Clean source worktree E:\codex\videoprocessor\subtitle-backing-binding-20261023, branch codex/subtitle-backing-binding-20261023. Readiness: exact current proof, stable layout and local application boundary are identified; reproduce one-pixel visible-edge clipping with current native pixel checks, stale/context/geometry negatives and GPU reconstruction regression. No change to Off fitting is planned.
