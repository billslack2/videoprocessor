# VP-0222: Withdraw contradicted pillarbox authority on full-width menus

## Status

Review (2026-10-30). Implemented and deployed at user request from clean x64 Release commit `c8b4943b81c3f9f5df5812609fd891e23ae6401c`; [draft PR #237](https://github.com/billslack2/videoprocessor/pull/237). All 669 affected native detector/transition/queue/OSD tests passed; standard and beta renderer matrices each passed 208 tests. Host and both installed renderer variants were backed up, replaced, hash-verified, and restarted. Live host and renderer logs identify c8b4943 with dirty=0; current menu is provisional rather than retaining the obsolete 1.66 crop. Broader live content/user acceptance and merge remain pending.

## Problem and evidence

Netflix menu side gutters acquired 136,0-3704,2160 (1.65185, labeled 1.66) despite rejected vertical-axis evidence. Later full-width menu frames remained provisional because of bottom darkness; both deferred and provisional paths retained the obsolete aspect indefinitely. Native capture `analysis-20261030-151313-29244-1` has visible picture in 88-93 percent of old side bands. Unmodified model reproduction retained the old crop for 6000 provisional observations, recovering after two authoritative full-raster observations.

## Acceptance criteria

- Current native broad picture evidence on both excluded side bands invalidates stale pillarbox authority after adjacent source confirmation; uncertain vertical boundaries cannot preserve disproved horizontal bars.
- Live and queued state invalidate together, and unavailable measurements never fabricate a replacement ratio.
- Reject incomplete-axis pillarbox acquisition without changing supported ratios or the confirmation latency for genuine measured pillarbox pictures.
- Cover native captured menus, genuine 1.66 and 4:3, real aspect changes, darkness, localized overlays, source repetition/gaps/generation changes, and queued publication.
- Build x64 Release, run applicable native and renderer regressions, back up and deploy the host/renderer pair, verify hashes and startup. Preserve user configuration.

## Tracker audit

Before allocation: 239 canonical files and 239 index rows, no duplicate or missing IDs; highest root 0221. Existing unrelated VP-0207 asset edit preserved. Pre-existing VP-0211 index/state mismatch and noncanonical Status formatting on VP-0070/0088/0168 are outside this change.

## Implementation progress

2026-10-30: Source commit `c8b4943b` pushed in [draft PR #237](https://github.com/billslack2/videoprocessor/pull/237), including VP-0194 OSD commit `2ce63718`. All 669 affected detector/evidence/crop-policy/timeline/OSD regressions pass. Original native capture replay confirms withdrawal at accepted source sequence 24594 after the first available three-frame contiguous run; 97 original frames inspected. Native P010/P210 1080p/2160p 1.66-adjacent and 4:3 positive controls retain four-observation acquisition. Initial implementation required complete full-axis support; capture replay demonstrated dark lower menu pixels correctly withhold that certificate, so withdrawal uses majority picture in each old side band while keeping source continuity, native provenance, complete scans, and broad luma/texture support strict.

Clean committed x64 Release solution build passed, including both renderer variants. Generated host/renderer version headers identify c8b4943 with dirty=false. Dual renderer GPU/cue validation and authorized paired deployment are in progress. No configuration changes planned. Remaining ambiguity deliberately reports unavailable until a replacement boundary is measured; withdrawal does not force 16:9.

## Deployment and final validation

- Clean source: `E:/codex/videoprocessor/menu-pillarbox-evidence-20261030`, branch `codex/menu-pillarbox-evidence-20261030` based on fetched beta d8518ef8; includes VP-0194 OSD fix 2ce63718.
- Backup and deployment receipt: `C:/Videoprocessor/vp/backups/20261030-163352-VP0222/deployment.json`. Previous host, both renderer DLLs, runtime records, configuration, and state retained there.
- Host SHA256: `56F908548AC930CC5B08E9B7528C3F82DB8C375EFAE54F2437E42BC0E15E594F`.
- Standard renderer SHA256: `04E05E4DB6969A3F22C5F81C5524A5AC91AD320C37AD0B34E4516C36D42D3CAD`.
- Beta renderer SHA256: `696EA444149F3B3F131ECFE64117D0DFF4F6237730441DFE73834310D2AF494A`.
- Configuration and state are byte-identical to their pre-deployment snapshots. No configuration edits. Existing libplacebo 360/388 dependencies match the build bundles.
- Test receipts: `x64/Release/menu-fix-tests/detector-final.trx` and `x64/Release/menu-dual-renderer-tests/motion-renderer-tests.json` in the source worktree. Dual receipt records sourceDirty=false and 208/208 passes for both dependencies.
- Restarted process 36836 is responding; live output/capture sequences advance and the current menu reports provisional full-width measurements, stable=0. Startup source identities and loaded module paths verified. Window screenshot automation timed out; no visual playback acceptance is claimed.
- Ambiguous remaining edges can legitimately leave the OSD Unknown; no replacement aspect is forced. Real-content changes across playback still need user observation beyond the native fixture/capture and controlled GPU tests.
