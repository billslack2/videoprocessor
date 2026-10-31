# VP-0222: Withdraw contradicted pillarbox authority on full-width menus

## Status

Review (2026-10-31). Implemented, reviewed and deployed from clean x64 Release commit `27dc733ad6b7f5afa01a13c1a114ff9337cc40a8` on [draft PR #241](https://github.com/billslack2/videoprocessor/pull/241). All 3,621 standard methods executed: 3,620 passed, with one investigated pre-existing configuration-cache failure retained as an explicit local-deployment exception. Actual plugins 20/20, standard and beta GPU suites each 209/209, focused regressions 941/941 and native capture replays 3/3 passed. Host/both renderers, live output and final configuration/state hashes verified. User visual acceptance remains pending; keep PR draft and unmerged. [Final report](../assets/VP-0222/ar-continuity-20261031/validation-report.txt).

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


## Combined two-field OSD deployment (2026-10-30)

The subsequent VP-0194 deployment at `955a8d54` includes this detector fix plus the submitted/live AR OSD and preserves the previously installed capture statistics/recovery fixes. It supersedes the local `c8b4943` runtime pair. All 1,039 selected regressions and 208 tests for each renderer dependency passed. Backup/receipt: `C:/Videoprocessor/vp/deploy-backups/ar-complete-20261030-164125/deployment.json`. Live rendered output confirms native 1.778 startup geometry while the Netflix menu candidate remains explicitly provisional; configuration/state are unchanged. See VP-0194 for full verification evidence.


## Eternals acceptance failure and wider audit (2026-10-30)

- Prior menu-fix validation remains historical evidence: Review (2026-10-30). Implemented and deployed at user request from clean x64 Release commit `c8b4943b81c3f9f5df5812609fd891e23ae6401c`; [draft PR #237](https://github.com/billslack2/videoprocessor/pull/237). All 669 affected native detector/transition/queue/OSD tests passed; standard and beta renderer matrices each passed 208 tests. Host and both installed renderer variants were backed up, replaced, hash-verified, and restarted. Live host and renderer logs identify c8b4943 with dirty=0; current menu is provisional rather than retaining the obsolete 1.66 crop. Broader live content/user acceptance and merge remain pending.
- OSD smoothing only changes displayed detected numbers. The native replay proves the current paused failure independently of OSD state, screen dimensions, and retained renderer history. The latest 16x9 profile disables automatic cropping, explaining its applied full-raster geometry but not the erroneous detected ratio.
- Investigate measurement thresholds, outward confirmation, near-black episode release, and profile-reset reacquisition together. Test low-contrast source pixels, local bright overlays, smooth gradients, real nonstandard aspects, and current-source/gap/provenance guards.
## Eternals correction deployment (2026-10-30)

- Commit: 2abdf66e6593f36d55d0337442782d0ab2d86b6d, draft PR #238. Audit: docs/LOW_CONTRAST_AR_AUDIT.md. This correction does not claim all dark-scene or format cases are resolved.
- All 97 captured frames acquire guarded native bounds 0,60-3840,2096 (1.88605:1, named 1.90:1 Digital IMAX); all source columns remain. Retained-Scope replay releases after three adjacent observations, with source gaps resetting proof.
- 716 selected native tests passed. Full GPU suites were not repeated per user instruction. Host, standard renderer, and beta renderer x64 Release builds completed from clean source; runtime records and installed SHA256 hashes match.
- Backup and receipt: C:/Videoprocessor/vp/deploy-backups/eternals-ar-20261030-222807/deployment.json. Configuration and state were backed up and preserved byte-for-byte during deployment.
- Restarted deployed process 29220 responds. Live host and renderer identify 2abdf66e with dirty=0; correct installed modules are loaded and output sequences advance. Live native/shared picture bounds remain 0,60-3840,2096. The existing 16x9 presentation selection was preserved; no live moving Scope-to-IMAX transition or visual OSD acceptance is claimed by this restart check.

## User acceptance and beta merge (2026-10-30)

Done (2026-10-30). User accepted the deployed result ("this all looks good") and authorized merge. PR #238 merged into v1.4.02-beta at a591ad3b532771911e85f8ef026958279f2065f0. The merge tree is identical to tested/deployed source 2abdf66e6593f36d55d0337442782d0ab2d86b6d. Current correction passed 716 selected native regressions and clean paired x64 Release builds; earlier OSD and integration validation remains recorded below. No additional playback restart or deployment was required for this merge.

[PR #238](https://github.com/billslack2/videoprocessor/pull/238). User acceptance does not assert exhaustive coverage of every dark scene or format. Prior review evidence: Review (2026-10-30). Follow-up 2abdf66e corrects low-contrast native boundaries, carries their proof through expansion confirmation, and releases obsolete retained near-black crops. All 716 selected native regressions pass, including all 97 captured Eternals frames and retained-Scope component replay. Host and both renderers were deployed from verified clean x64 Release builds at 22:28 EDT. Live host/renderer identity and corrected native geometry 0,60-3840,2096 are verified; real moving Scope-to-IMAX transition acceptance remains for playback review.

## Post-merge provisional-retention audit (2026-10-30)

In Progress (2026-10-30). Reopened for the user-requested four-reviewer Sol medium audit after a new Prime incident: provisional 2.537:1, saved native picture about 2.33:1, and applied full-raster fallback despite safe retained bands. The accepted merge a591ad3b remains historical; current audit starts from its freshly fetched beta tree in E:/codex/videoprocessor/ar-policy-audit-20261030. Review measurement, retention, renderer integration, and test contracts; run existing native regressions and independently validate findings before proposing code changes. No new deployment is authorized by this audit task.

Incident logs retained locally at C:/Users/bslac/.codex/visualizations/2026/10/30/01a18e4e-a033-7062-8c14-67fb1a200a5f/prime-254-incident-20261030.log. Current incident runtime is 82e3fc0a (subtitle follow-up), with unchanged ActivePictureEvidence, ActivePictureTransitionModel and AlphaSourceCropPolicy relative to beta. Do not infer nominal aspect from the screen or streaming provider.
## Four-reviewer audit results (2026-10-30)

- Four gpt-6.1-sol / medium reviewers covered native measurement, retention, renderer integration, and test contracts. Parent independently reviewed all findings and narrowed low-level withdrawal concerns using the final renderer path.
- P1: the continuity anchor cannot first acquire after normal BAR_REFINEMENT or FIT when actual bounds differ from native. Incident logs show available=0 before and after the drop. A production-component probe returns anchor reason actual-geometry-not-qualified and final full raster, without confirmed full-raster authority.
- P1: symmetric low-contrast picture can already become BAR_CROP_TRUSTED, bypassing provisional-only recovery. Synthetic 640x360 input with real picture32..328 is accepted as72..288: actual2.16216 versus trusted2.96296.
- P2: Full-range bars at code12 with lower picture at22 remain provisional46..300 instead of physical46..314, because recovery treats threshold reference0 as actual matte floor. This is missed recovery, not an accepted false crop.
- Additional concern: exact-source positive outside-pixel extents are ignored without separate caption/FIT ownership. Existing tests bless this behavior. A native source fixture is still needed to distinguish picture/caption preservation from transient/noisy raw evidence before modifying that contract.
- Existing unchanged-beta x64 Release suite: 993/993 pass in38.7823s, including the additional continuity/ownership test classes omitted from the earlier716 selection and both asserted Eternals capture replays. All three new desired-behavior probes fail against unchanged production code. Probe patch saved; temporary test edits restored. No broad GPU matrix or live playback test performed for this audit.
- Report: C:/Users/bslac/Documents/ChatGPT/Done/VP-AR-audit-20261030.txt. Probe patch: C:/Users/bslac/Documents/ChatGPT/Done/VP-AR-audit-20261030-probes.patch. Baseline and probe TRX/build logs: E:/codex/videoprocessor/ar-policy-audit-20261030/x64/Release/ar-policy-audit.
- The logical transition model preserves stable geometry and OSD in-use follows submitted crop; neither was established as the cause. Do not fix the incident by delaying the OSD or forcing a preferred aspect. Current native pixels and actual submitted continuity must govern change.
## Stable framing and real-transition architecture (2026-10-31)

- User requested quality architecture supporting both stability and genuine AR changes. Native, retention and test reviewers supplied design/red-team follow-ups; parent reconciled them with the verified audit.
- Separate accepted native geometry, current typed per-edge evidence, last successful presentation, and temporary content-preservation extent. A valid first refinement/FIT can commit actual presentation together with its independently accepted native contract; exact native bounds need never have appeared on screen previously.
- Coarse scanning only nominates. Every candidate, including the ordinary trusted path, passes common excluded-band/boundary verification using measured matte statistics distinct from black classification thresholds. Unknown never means contradicted or full raster.
- A single native transition decision preserves established authority through uncertainty. Qualified outside pixels may expand a bounded presentation before native AR commitment; overlays never vote for native aspect. Positive picture evidence retains a prompt transition route, including dim/asymmetric changes. Genuine windowbox needs a verified native route separate from composition presentation choices.
- Preserve source/format/sequence and successful-submission guards. Profile changes recompute presentation from compatible accepted source geometry. Keep existing confirmation settings as initial compatibility values; do not add blanket delay or serial duplicate dwell.
- Implementation plan: failing regressions/final-submission harness; continuity repair; shared candidate validation and measured-floor separation; qualified outside-content preservation; consolidation of transition decisions. Include false crop/full-raster counts, pixel clipping, edge error, source sample count and final-submission latency.
- Design asset: [ar-architecture-20261031.txt](../assets/VP-0222/ar-architecture-20261031.txt). User copy: C:/Users/bslac/Documents/ChatGPT/Done/VP-AR-architecture-20261031.txt. This is a design, not a tested implementation; prior993-pass/three-failing-probe receipts remain audit evidence only. No code or deployment changes in this architecture task.
## Reviewed implementation candidate (2026-10-31)

- Clean source commit `19c46962f240cf75f721661edfc827504aa8faab`, branch `codex/ar-evidence-continuity-20261031`, [draft PR #241](https://github.com/billslack2/videoprocessor/pull/241). Current GitHub beta remains `a591ad3b`; installed subtitle correction `82e3fc0a` is preserved.
- Common native-band verification uses measured matte levels, bounded sparse-overlay support, and matte-relative connected-picture checks. Accepted native geometry is independent of the exact submitted FIT/fill/translation. Strong exact-source outside-picture proof preserves pixels before the existing three-vote AR publication. Missing analysis retains compatible accepted geometry; source/format/domain changes clear it.
- Four Sol medium reviewers contributed the audit and implementation review. Cross-review found and fixed first cold-caption FIT ordering and a dim connected-picture hole in sparse-overlay handling. Final review has no remaining blocking findings.
- Focused development tests passed 559 detector/policy cases and 20 actual-renderer/OSD cases, including both renderer DLLs, scope-to-IMAX and return, dark retention beyond three seconds, and distinct native versus configured-fill AR. Three saved-capture replay methods passed, covering all 97 Eternals and 97 Netflix frames.
- The complete clean-commit native/stack suite and both GPU dependency matrices are still running or pending. Initial full development run was intentionally stopped after the focused run exposed five failures; those were corrected and their failure receipt retained. Do not count that interrupted run as a full pass. Final deployment and user visual acceptance remain pending; configuration has not changed.


## Combined candidate and regression reconciliation (2026-10-31)

- Final implementation candidate: `27dc733ad6b7f5afa01a13c1a114ff9337cc40a8`. The newer deployed subtitle cadence correction `7d9b26df` was detected during validation and preserved by cherry-pick `477fdfb6`; no installed subtitle work will be rolled back.
- Common verification now keeps a partial windowbox nomination provisional when either nominated cropped axis is unverified. It cannot relabel a safe full-raster fallback on one axis as measured native picture width/height.
- Caption continuity carries only independently verified caption extent/padding, bound to successful submission and source/format/domain/epoch. Native expansion and later publication retain current native geometry while preserving an unresolved cue. Fresh replacement and exact current prepared output have dedicated interaction regressions; configured fill cannot discard the protected extent.
- The first broad candidate run completed 3,354/3,370 tests; its 16 failures exposed obsolete diagnostic nominations/sample bounds and unsafe fixture assumptions. Updated tests explicitly distinguish coarse synthetic nomination from fresh native recovery, retain negative controls, and verify the pixels actually kept. Four additional policy conflicts from caption integration were corrected without relaxing their legacy assertions. The complete focused rerun passed 941/941.
- Clean candidate native capture replay passed 3/3 methods with all saved Eternals/Netflix frames. Clean candidate host, standard renderer and beta renderer Release builds passed. Runtime receipt: `E:/codex/videoprocessor/ar-evidence-continuity-20261031/x64/Release/ar-validation/release-build-passed.txt`.
- A final-suite attempt overlapped compilation and was intentionally stopped after converter timing and renderer-DLL availability failures. It is not counted as full validation. Evidence remains in `interrupted-concurrent-full.log` and `interrupted-concurrent-run.txt`. The comprehensive rerun is serialized after other active validation, followed by actual-plugin and both GPU dependency runs. Deployment is still pending.


## Final validation and visual-test deployment (2026-10-31)

- Exact source `27dc733ad6b7f5afa01a13c1a114ff9337cc40a8`; all four reviewer disciplines and parent review completed. Prior deployed subtitle corrections remain included.
- Complete standard inventory: 3,621 executed, 3,620 passed, no omissions. Comprehensive 3,391/3,392; actual-plugin 20/20; standard GPU 209/209. Additional beta GPU 209/209, native replays 3/3, focused regressions 941/941.
- The original comprehensive failure is unchanged `SameSizeEditWithRestoredWriteTimeInvalidatesCache`. Same-binary isolated class passed 5/5, but a metadata collision reproduction confirms an existing defect, not a fixed flaky test. Original failed evidence remains intact. [Known cache defect](../assets/VP-0222/ar-continuity-20261031/known-cache-defect.txt).
- Paired host/standard/beta Release deployment at 2026-10-31T03:42:57.3764807-04:00. Backup: `C:\Videoprocessor\vp\deploy-backups\ar-continuity-20261031-034256`. Installed hashes and embedded/full runtime commit identities verified. No config edits. Startup selected Classic; one verified live profile cycle restored Reconstruction, after which both cfg and state match pre-deployment hashes.
- Process 33244 responds, expected module paths are loaded, source/output sequence advanced 4283 -> 4884. Other portable processes were untouched.
- [Validation and deployment report](../assets/VP-0222/ar-continuity-20261031/validation-report.txt); adjacent JSON receipts pin hashes, source, test counts and runtime verification. No captured movie pixels are committed.
- Visual scope/IMAX, Prime paused/profile-toggle and menu/caption acceptance remain pending. Exact Prime source pixels were unavailable. Draft PR remains unmerged for user review.

### Combined rebuild requested with subtitle fixes — 2026-10-31 08:52 EDT

User requested a fresh combined build/deployment and explicitly skipped tests. Rebuilt the host and both renderer variants in x64 Release at the same reviewed combined commit `27dc733ad6b7f5afa01a13c1a114ff9337cc40a8`, starting a clean worktree from the freshly fetched current beta before integrating this candidate. The complete subtitle cadence/three-line repair is included. No new source changes, test runs or beta merge.

Deployed with verified hashes and clean host/renderer startup identities; PID4616 is presenting output. Configuration and saved state are unchanged, including Classic subtitles. Backup: `C:\Videoprocessor\vp\backups\combined-subtitle-ar-20261031-085218`. [Shared deployment receipt](../assets/VP-0207/combined-subtitle-ar-20261031-0852/deployment-receipt.json), [runtime verification](../assets/VP-0207/combined-subtitle-ar-20261031-0852/runtime-verification.json). Story remains Review; physical user acceptance remains separate.
