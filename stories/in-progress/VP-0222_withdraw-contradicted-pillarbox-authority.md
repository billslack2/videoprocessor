# VP-0222: Withdraw contradicted pillarbox authority on full-width menus

## Status

In Progress

Latest implementation (2026-11-01): active spatial/subtitle repair deployed for local review from clean x64 Release commit 82a0f8c7. Default-mode full qualification, active-mode renderer/capture checks, and broad GPU results are recorded separately in [stage2 evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage2.json). The repair preserves independent source measurement and current subtitle ownership, removes redundant startup votes only for verified spatial crops, and retains exact completed output on the eligible skipped frame. Deployment made no configuration/state edits; Classic remains selected. Story remains In Progress pending user playback acceptance and real dynamic-transition coverage; no merge/public release.

Previous qualification record (superseded by failed live acceptance below): In Progress (2026-10-31). Reopened after failed live scope-to-IMAX acceptance of combined candidate 27dc733a. Repair and focused regression checks are underway in E:\codex\videoprocessor\combined-transition-regression-20261031. Prior controlled qualification below does not establish live acceptance. Previously implemented, reviewed and deployed from clean x64 Release commit `27dc733ad6b7f5afa01a13c1a114ff9337cc40a8` on [draft PR #241](https://github.com/billslack2/videoprocessor/pull/241). All 3,621 standard methods executed: 3,620 passed, with one investigated pre-existing configuration-cache failure retained as an explicit local-deployment exception. Actual plugins 20/20, standard and beta GPU suites each 209/209, focused regressions 941/941 and native capture replays 3/3 passed. Host/both renderers, live output and final configuration/state hashes verified. User visual acceptance remains pending; keep PR draft and unmerged. [Final report](../assets/VP-0222/ar-continuity-20261031/validation-report.txt).

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

### Combined rebuild requested with subtitle fixes â€” 2026-10-31 08:52 EDT

User requested a fresh combined build/deployment and explicitly skipped tests. Rebuilt the host and both renderer variants in x64 Release at the same reviewed combined commit `27dc733ad6b7f5afa01a13c1a114ff9337cc40a8`, starting a clean worktree from the freshly fetched current beta before integrating this candidate. The complete subtitle cadence/three-line repair is included. No new source changes, test runs or beta merge.

Deployed with verified hashes and clean host/renderer startup identities; PID4616 is presenting output. Configuration and saved state are unchanged, including Classic subtitles. Backup: `C:\Videoprocessor\vp\backups\combined-subtitle-ar-20261031-085218`. [Shared deployment receipt](../assets/VP-0207/combined-subtitle-ar-20261031-0852/deployment-receipt.json), [runtime verification](../assets/VP-0207/combined-subtitle-ar-20261031-0852/runtime-verification.json). Story remains Review; physical user acceptance remains separate.

### Combined live regression reopened â€” 2026-10-31

User reports missed scope-to-IMAX expansion and severe vertical bouncing with combined 27dc733a. Focused regression checks are explicitly authorized, superseding the prior skip-tests instruction for that rebuild. Source work starts from the reverified beta a591ad3b and includes the failed combined candidate in E:\codex\videoprocessor\combined-transition-regression-20261031.

The saved native V210 frame reproduces the boundary failure against the unchanged deployed production library: nomination66..2092 is provisional, first mismatch323,65. Native pixel inspection finds3435 out-of-tolerance pixels excluded by those bounds; measured outward64..2094 excludes zero. Repair preserves strict verification and keeps bounded outward visibility separate from native authority. Independent policy review also reproduced Classic translation plus side-fill certificate mismatch, which restored an old vertical position on skipped scans.

User confirms View/fullscreen changes were intentional; those orderly renderer replacements are not an unexplained restart. Actual HDMI cadence also changed23.976/59.94 and the EPSON target was unavailable during fallback; no unsupported refresh-policy change is being made. Evidence is preserved at E:\codex\diagnostics\combined-live-regression-20261031-0859. New repair is not yet deployed or qualified.

## Critical live regression follow-up — 2026-10-31

Live acceptance FAILED for both repair deployments, 6cb6576cc1a0a3e313061da322dd64776fa62e27 (10:15) and 60cbfacee2c531eb138cdeaa12551c10f9569b8a (10:29). The user reports repeated Classic picture/AR bouncing on Mission: Impossible. Further deployment is held until realistic reproduced failures pass. No beta merge or live acceptance is claimed.

The 60cb candidate passed 1,072 focused framework methods (1,066 exercised, six optional historical capture fixtures absent) and 22 actual-plugin methods. Those methods missed normal 500 ms engage / 2,000 ms release timing and narrower/top-bottom fill. Prior 6cb dual-renderer GPU/cue results (209 each) remain historical evidence, not qualification of later edits.

Four new actual-renderer lifecycle tests now fail on the exact installed 60cb standard/beta DLLs: both renderers with wider and narrower fill. They acquire native geometry then resize on the first caption frame before the configured drift begins. Assertions include fixed viewport dimensions, gradual movement, hold/release, 24 distinct displayed source frames after transitions (not a runtime delay), genuine picture expansion, and a Classic-to-Reconstruction switch. Red results and hashes: E:/codex/diagnostics/combined-live-regression-20261031-0859/classic-lifecycle-60cb/. Latest logs and recordings: E:/codex/diagnostics/classic-live-regression-20261031-1035/. Sampled recording frames are black; logs independently document oscillation. No additional native capture was taken because the user had closed the player.

Review identifies post-black-level-fix presentation certification as the regression area: 19c46962 introduced strict final-pose matching; 6181deb9 supported translated side fill only; 60cb still assumed native height and unchanged translation offset. Live native 0,258–3840,1906 translated to 0,400–3840,2048 becomes configured final 0,406–3840,2040 and is rejected. Coarse opposite-edge nominations and first-caption pending ownership also need explicit current evidence. Preserve c48eb134 black-reference correction and strict native boundary verification; do not relax black thresholds or force a standard aspect ratio.

Current work: coherent final translation/fill provenance, current per-edge raw pixel evidence, caption lifecycle ownership, and mandatory build qualification covering both actual renderer variants. Source branch codex/combined-transition-regression-20261031 in E:/codex/videoprocessor/combined-transition-regression-20261031. Status remains In Progress; the installed build is still known failing.

### Classic lifecycle and queue-handoff repair qualification — 2026-10-31

Clean source `b52ae03a424fcd5557ebac6de2ba1c3b28448eee` is pushed on `codex/combined-transition-regression-20261031`; [draft PR242](https://github.com/billslack2/videoprocessor/pull/242) targets the reverified current beta `a591ad3b`. No beta merge, new deployment or physical playback acceptance is claimed.

The identical queued renderer matrix fails on installed60cbface, while both standard historical7d9b26df lifecycle cases pass. This brackets the regression after the black-reference correction without claiming an exact first-bad commit. The repair validates the entire current Classic translation plus configured fill, preserves the original caption measurement lease through admission, and retains raw current per-edge evidence independently of presentation masks. Real native picture expansion still overrides the old pose immediately;24displayed source frames are only a post-transition test observation window.

Enforced testing found an additional Reconstruction bounce after queue retirement: a valid cadence frame stripped its bar metadata, causing the source-template getter to hide its accepted identity. The pending guard and tracker also needed the same last-verified source. A native tracker-to-cadence-to-timeline reproduction now fails before repair and passes afterward. The separate private queue certificate authorizes only a queue-retirement witness; bar tracking, independent analysis, native AR authority and original expiry remain unchanged. Current glyph/chroma/bar mutation and context/identity/retry negatives remain required.

The clean candidate passed706native framework methods plus6actual-renderer lifecycle methods on EACH dependency (712/712 each),20additional plugin methods and397additional native methods. Exact saved native IMAX replay passed at64..2094. One optional historical dark capture in the mandatory set and five optional capture methods in the additional native set had no input and are explicitly unexercised. All324focused tracker/cadence/handoff/timeline methods passed the repair run, with overlap in the later matrix; counts must not be added as unique coverage.

The full209-per-dependency GPU/cue suites and final direct-project build-gate qualification are running serially. Failed earlier runs are preserved, and missing/stale/failing evidence blocks deployment and packaging. Evidence root: `E:/codex/diagnostics/combined-live-regression-20261031-0859/classic-final-qualification/`; native red/green evidence: adjacent `cadence-pending-repair/`. The installed player remains the known-failing60cb build and is closed. Status remains In Progress pending qualification and live acceptance. Existing unrelated design.txt changes are excluded.

### Qualified Classic/queue repair deployed — 2026-10-31 13:17 EDT

Completed qualification and deployed clean `b52ae03a424fcd5557ebac6de2ba1c3b28448eee` from x64 Release. Both the solution build gate and the direct-project build gate passed the required 712 framework methods on each dependency (706 native and six actual renderer methods). Both complete GPU/cue suites passed 209/209; the additional plugin set passed 20/20. Additional native methods returned 397 framework passes, of which 392 were exercised and five optional private replays were absent. The mandatory set includes one absent optional historical dark replay per dependency; the saved native IMAX replay was exercised and passed on both. Counts overlap and are not an additive unique-test total.

The matched host and both renderer variants, runtime records and symbols were backed up and deployed; all 10 installed file hashes match their qualified artifacts. Backup: `C:\Videoprocessor\vp\backups\classic-lifecycle-repair-20261031-131745`. Configuration edits: none. Active cfg SHA256 remains `2F644385C45D532CB213DFD4C432B95398467FADFC8FBD25BD41C48A946943C5`; saved state remains `99CF94A66291AC295858C9AFD73DC880EE42F9496FD974D9CC44A9A1BF979222`. The primary player was already closed and was left closed; unrelated portable processes 23240, 24680 and 25508 were untouched. No startup or physical playback acceptance is claimed.

[Final explanation and limitations](../assets/VP-0207/classic-lifecycle-20261031/validation-report.txt), [pinned validation](../assets/VP-0207/classic-lifecycle-20261031/validation-report.json), [mandatory runtime receipt](../assets/VP-0207/classic-lifecycle-20261031/mandatory-runtime-qualification.json), [dual GPU receipt](../assets/VP-0207/classic-lifecycle-20261031/motion-renderer-tests.json), and [deployment receipt](../assets/VP-0207/classic-lifecycle-20261031/deployment-receipt.json). Full logs, red/green TRX files and native movie pixels remain local in the diagnostic evidence tree. [PR242](https://github.com/billslack2/videoprocessor/pull/242) remains draft/unmerged; status stays In Progress pending user replay. The separate HDMI refresh/EPSON fallback issue and unrelated historical broad-suite failures are not claimed fixed. The black-reference correction is retained, and the 24-frame assertion adds no runtime waiting period.

### Live acceptance FAILED again — 2026-10-31 13:25 EDT

The user rejects deployed b52ae03a: subtitles or other black-bar content still cause repeated picture bouncing. This supersedes any interpretation of prior automated qualification as live success. Both stories remain In Progress; PR242 is draft and prominently marked failed live. No further repair is deployed.

Preserved current log/config/state/binary identities at E:/codex/diagnostics/blackbar-content-regression-20261031-1325/. The log confirms unchanged source3840x2160 v210 SDR Rec709 at59.94Hz, native250..1906 (2.31884, below configured2.32fill limit). Pure Classic repeatedly alternates translated318..1974 on scheduled analyses with outward126..1974 on skipped analyses; current raw content occupies both bars. No profile/input change explains the cycle. The prior matrix only used a single bottom caption and aspects inside the fill window.

Team review identifies scheduled controller confirmation bypassing the current opposite-bar visibility check, while skipped analyses enforce it. The new native regression fails against unchanged b52 behavior; the identical skipped-input case passes. New UHD actual-renderer tests use the live16-frame queue/9target, source geometry and both bar overlays. Initial fixture assertions conflated provisional raw aspect telemetry with acceptednative authority and translation metadata with actual bounds; these assertions are being corrected while preserving all visibility/stability requirements. A separate advanced/Reconstruction chain also needs reproduction: final composition changes the actual source bounds after the selected translation certificate was created, preventing the safe native presentation from replacing a previous outward-fit anchor. Production code remains unchanged pending faithful red tests for both paths.

### Current subtitle/AR integration repair — 2026-10-31

Deployed b52 failed live black-bar/subtitle acceptance. The current clean combined candidate is 4fa45da9342defc8914c45350b157e5de7b35177; 729 native contracts pass and full output-renderer/GPU qualification is pending. Current picture growth still overrides subtitle viewport continuity, measured native bounds remain independent of fitting, and no standard aspect is forced. See the [VP-0207 causal review](../assets/VP-0207/blackbar-viewport-20261031/causal-review.md). No new deployment, beta merge, physical playback acceptance or fix for the separate HDMI refresh/EPSON fallback issue is claimed. Status remains In Progress.

### Combined repair qualified and deployed — 2026-10-31 17:27 EDT

Clean x64 Release `4fa45da9342defc8914c45350b157e5de7b35177` is now deployed with both renderer variants. Complete final gate passes 729 native + 20 actual methods on each test variant; GPU/cue suites pass 209 per dependency. Native geometry and first current picture-growth priority are preserved independently of downstream fitting. [Final repair evidence](../assets/VP-0207/blackbar-viewport-20261031/validation-report.txt) and [deployment receipt](../assets/VP-0207/blackbar-viewport-20261031/deployment-receipt.json) record exact identities/hashes. Configuration/state unchanged; main player left closed. Physical scope/IMAX and subtitle playback acceptance remains unconfirmed, so status remains In Progress and PR242 remains draft/unmerged. The separate HDMI refresh/EPSON fallback issue is not claimed fixed.


### 4fa live acceptance FAILED; native boundary/cadence repair — 2026-10-31

The user rejects deployed `4fa45da9342defc8914c45350b157e5de7b35177`; its earlier automated qualification is historical, not live acceptance. The preserved 59.940060 Hz INPUT log shows the same persistent caption visible on odd inputs and unavailable on even inputs because `input-cadence-not-analyzed`, recovering16.7ms later. This is not expiration of the250ms hold. The later native capture has23.976Hz input and is separate evidence.

All64 supplied full-resolution decoded-DV frames measure picture263..1897 at3840x2160 (2.350061:1), separately from conservative guard254..1906. A provisional caption rectangle262..1976 was exposed as detected2.240373 while in-use aspect stayed about2.35. The repair separates exact current physical observation, retained guard, proposal and actual submitted geometry. Symmetric aligned fill preserves both picture edges; black thresholds and standard-ratio selection are not relaxed or forced.

The actual old renderer reproducibly bounces crop262..1898 to262..1954 on a fixed three-line DV source, with a sampled cadence-current-cleanup-changed rejection(165126samples). Independent CPU preflight proves all3lines, native decoded pixels, measured cleanup crossing changing picture and a real component-budget UNKNOWN input. Earlier harness setup failures are not accepted as cadence reproductions.

Clean `981eef155601be3fd4a2f36fcb3953bdd9d21740` is pushed on `codex/native-boundary-cadence-20261031`, based on reverified beta`a591ad3b`. [Draft PR243](https://github.com/billslack2/videoprocessor/pull/243) supersedes [PR242](https://github.com/billslack2/videoprocessor/pull/242). Full-rate analysis is the default; one inconclusive progressive input may repeat the prior complete successful final image on either D3D11 presenter. Original picture/cue/crop/HDR ownership and expiry stay original, with consumed input separate. Publication races cannot erase completed physical observations; source measurement is independent of crop/fitting settings.

The selected native/copy run passes792/792, including all790 mandatory native methods, all64 private frames and3line preflights. Every runtime qualification now requires790native+28actual methods per dependency, rejects missing/unexecuted/stale evidence and diagnostic old-plugin overrides, and checks the pinned backend submission contract. Matched x64 Release build and actual renderer qualification are in progress; full dual GPU checks and real one-image-repeat coverage remain required before deployment. No new deployment, beta merge or physical acceptance is claimed. Current playback/configuration stay untouched. Evidence: E:/codex/diagnostics/cadence-live-failure-20261031-1734/.


#### Release candidate 981eef15 rejected by the new throughput test

The matched Release host and both renderer variants compile successfully. The new actual default-presenter59.94Hz test fails before any processed cue: current scans take21–25ms, futureAvailable stays0 and matchingFrames stays1. A single worker cannot sustain this three-line workload at60Hz. The full qualification run was interrupted after compilation to prioritize this new failure; no passing receipt or deployment is claimed.

A bounded two-worker repair is being implemented for inputs above30Hz, retaining one serial sampler at30Hz and below. Test-first evidence proves high-rate concurrency/out-of-order availability and nonadjacent tracking-reference failures while low-rate control passes. Workers keep independent mutable scanners; every result remains bound to its exact source/context. Confirmation and pixel-safety requirements remain unchanged. An additional CPU fixture alternates real moving non-glyph backing while requiring all three stable glyph lines. [PR243](https://github.com/billslack2/videoprocessor/pull/243) remains draft and explicitly unqualified.


#### Full-rate worker checks pass; candidate still rejected by actual held-caption crop test

Candidate `5f950365` is pushed to draft PR243. No repair has been deployed. `parallel-final-native.trx` passes 800/800 methods, including all 798 required native contracts, the 64 private native frames, alternating three-line decoded-DV evidence, two-worker throughput, exact out-of-order identities and diagnostic capture ownership. Matched Release host, both renderers and both test variants compile.

Preparation tracing distinguishes startup from the steady failure: one stable shader key dispatches successfully, remains Pending, and completes after 122.1 seconds. Two shader topology compilations dominate; device setup, normalization and final viewport preparation are fast. The harness now allows a separately bounded 180-second cold startup, checking every fallback output for picture/caption preservation, followed by an independent steady-state observation budget. This does not remove the cold-start performance concern or weaken post-readiness checks.

The actual default-presenter test then reproduces crop bounce in the candidate: source4880 presents native262..1898; source4947 has fresh three-line measurements and matching glyph pixels but tracker-held geometry (`strict-current-ink`), loses current caption presentation proof, and expands the displayed bottom to1954. Thus full-rate scanning alone is insufficient. Reviewers are designing an exact-current existing-cue ownership certificate for held geometry, usable only for prepared erasure after all existing current-pixel cleanup/residual checks. It must not authorize stale glyphs, new cues or native AR/lease renewal. Consecutive healthy held sources and changed/subset glyph vetoes require focused contracts. An isolated whole-output repeat cannot substitute for that proof repair.

Full qualification, both GPU/cue suites and actual one-input repeat/recovery remain pending. There is no passing runtime receipt. Active user configuration and playback remain untouched; the selected subtitle profile was observed as Classic during this checkpoint. The Classic persistent fixture also needs to distinguish the configured initial 500 ms engage/three-frame translation confirmation from post-settle instability; its first-caption failure is not yet accepted as evidence of persistent bounce.

#### Manual review rejected AR behavior; faster iteration and picture-only repair — 2026-10-31

The explicitly requested manual-review build `4336d9ad` was deployed with incomplete full qualification recorded honestly. The user reports reduced flicker but unacceptable AR delay and caption-driven picture expansion in Reconstruction. This is failed live acceptance, not a completed repair. Active configuration was preserved; backup is `C:/Videoprocessor/vp/backups/manual-boundary-cadence-20261031-211646` and private evidence is `E:/codex/diagnostics/cadence-live-failure-20261031-1734/manual-433-20261031-2126/`.

Candidate `6d4990a8` passes all 819 mandatory native contracts, including the 64 supplied native frames. Failing-first tests reproduced: already-measured lookahead discarded by an 80 ms scan gate; complete current native picture changes delayed by old subtitle owners/drift; advanced uncertainty expanding bottom1904 to2028; native growth2040 inflated to2120 by captions. Repairs consume already-computed exact lookahead without rescanning, supersede old caption timers only with complete independent current picture proofs, and keep advanced presentation bounds independent of caption evidence. A separate full-raster equality defect in the current picture-preservation proof was corrected. No standard ratio is forced. The 2-second unknown/scene guard was retained because review found no proof that it overrides a successfully published new native geometry.

Several historical advanced viewport tests encoded caption-expanded picture geometry; their expectations now enforce native-only picture geometry while keeping source/context, instability and current-pixel safety cases. The operator explicitly confirms that caption clipping during shader compilation is intentional and acceptable. It is not a deployment blocker. The startup oracle permits this only for an exact current Missing/Pending shader key; failed preparation and post-ready caption/cadence gaps still fail. No new basic-compositor fallback is being built.

The user prioritizes faster build/test turnaround. New Native and Focused lanes preserve exact source/binary/TRX evidence without issuing a full qualification receipt. The dedicated Native target compiles only manifest-selected test units plus required support (13 units after the new owner-proof source, versus137 before), with four compiler workers. Shared dependencies finish before independent companion builds run two at a time. Actual Standard/Beta proxy cases now execute once each in their matching test host instead of twice; native coverage remains in both ABI hosts for full qualification. A writable test shader-cache override avoids repeated lost caches relative to Visual Studio testhost. Full qualification always starts with a unique empty application cache, so cold warmup remains covered; focused runs can explicitly request cold cache or reuse a warm cache.

Measured native validation at6d4990a8: native library1.606s, dedicated test build25.432s, all819 contracts passed. Earlier805-method native execution was36.03s versus175.41s for one two-rate shader-heavy actual test. Workflow tests verify method-selection completeness, concurrency/dependency barriers, literal arguments, failure draining and receipt rejection. `8e949d14` changes only version-generation tool invocation after the first parallel Release attempt exposed PowerShell7 vs Windows PowerShell quoting. Matching x64 Release host/renderers and focused cold-shader/cadence checks are running. No new deployment, full qualification, merge or completed live acceptance is claimed. Draft PR243 remains In Progress.

#### Declared-input cadence repair and manual build ced983fd — 2026-10-31

A warm-cache reproducer exposed a real startup bug: complete-output retention was gated on measured capture rate, which remains zero until five seconds of source time. The cold shader build masked this interval. Candidate `ced983fd` uses the declared source timeScale/frameDuration for repeat eligibility immediately; measured telemetry remains separate and the identity, geometry and expiry checks are unchanged. The failing-first CPU reproducer expected60Hz and received0 before repair.

All821 native contracts pass, including replay of all64 captured source frames. Four actual renderer checks pass: Classic persistent captions and controlled unknown-input complete-output repeat/recovery on both Standard and Beta renderers, each at59.94/60Hz. The focused matched Release build/check cycle took119.86seconds, including18.04seconds for the four renderer tests with warm application cache. Method-selected Native builds now compile only owning translation units plus required support.

`ced983fd` was deployed for the already-authorized manual review with matching x64 Release artifacts and no configuration edits. Backup: `C:/Videoprocessor/vp/backups/manual-boundary-cadence-20261031-222515`. Deployment receipt: `E:/codex/diagnostics/cadence-live-failure-20261031-1734/manual-deployment-ced983fd.json`. This is not full qualification or live acceptance.

The subsequent full cold-cache qualification FAILED10 Standard actual tests: Classic lifecycle onset size, source-native acquisition, and the Classic onset portions of UHD caption/UI tests. The four persistent/cadence tests passed in that run; Beta full suite was not executed after Standard failure. Review is separating genuine Classic onset regressions from fixture assumptions. No full qualification receipt, merge or release publication is claimed. The explicit shader-compilation clipping exception remains; it does not exempt post-ready failures or Classic behavior unrelated to shader compilation.

#### Live subtitle improvement; generic boundary review with five Luna extra-high reviewers

The operator reports that installed ced983fd now handles fixed-AR subtitles well: captions are stable and do not visibly change picture AR. This is positive scoped manual evidence. AR acquisition/Unknown and occasional transition flash remain open; it is not full live acceptance.

Two new captures distinguish separate cases. analysis-20261031-225331-38828-1 contains95V210full-raster sun/space frames; dim left-edge support makes physical confidence Unknown despite native full-raster presentation. analysis-20261031-230055-38828-2 contains56decoded-DV frames with stable native guard274..1884 and measured edges275..1884 when accepted.590of720physical evaluations reject the near-black outer reference because one chroma range spans5codes versus strict4. All56sidecars carry274..1884; they must not be conflated with the previous V210capture's full-raster sidecars. The52.47second recording decodes black at every sampled second; screenshots/native captures remain usable. No logged intermediate crop was established for the examined scope-to-full transition.

A DV-only noise exception was rejected by the operator and removed before deployment. Five explicitly requested gpt-6-luna/xhigh reviewers completed research on advanced detection, lookahead, hints/history, spatial false positives, and integration. The agreed direction is generic current-source strip statistics, local noise estimation, distributed edge agreement, occlusion masks carried through every vote, earliest sustained boundary rather than strongest interior gradient, and bounded full-hint nomination. Known and exact remembered ratios prioritize hypotheses/search regions; current pixels still establish measured bounds and the reported ratio. Already queued3/5-frame depths are opportunistic evidence, not a new3-of-5 or24-frame runtime wait. Keep physical estimates, conservative guards, native authority and submitted fitting distinct within the existing immutable source-bound evidence pipeline.

Research identified existing useful infrastructure: source-bound cache/worker evidence, two-observation ready-lookahead timeline, exact remembered geometry with current pixel verification, and strict selected-output identity. The limited SDR floor logic predates the Full-range black-level change; the newer physical observer introduced the strict reference check. This does not establish a single causal commit for every reported playback regression. Generic tests and source-capture replay will precede a boundary implementation; false-crop controls remain required.

14a4f565 full Standard qualification passed835/838; failures were retained advanced-fill certification and two startup/availability cases. The retained-fill bug is now reproduced and repaired in uncommitted source with typed current admission/actual Commit tests. A warm focused rerun passes all3previously failing actual methods. A cold focused rerun passes retained-fill lifecycle but fails CrossEdge preparation deadline and Black first-ready caption availability; these remain explicitly unresolved, not reclassified under the operator's shader-building clipping allowance. No passing full receipt, new deployment, merge or release publication is claimed. Draft PR243 remains In Progress.

#### Generic source-border repair a2326513: native/capture GREEN, runtime qualification running

Source commit a23265130f2a543597d7502434095cbb23be3bf6 is clean and pushed on codex/native-boundary-cadence-20261031. The generic observer uses robust reference strips and independent holdout rows, carries occlusion masks through boundary votes, preserves the first sustained edge, and rejects persistent dim collars and detached graphics. Full-raster hints can nominate encoded bars. Horizontal searches use the currently measured vertical interval, preventing top/bottom bars from becoming false side-edge evidence. An unqualified optional terminal nomination falls back only to the existing current-pixel extent proof. No DV-only exception, ratio snapping or additional temporal dwell was added.

All 837 native methods pass (20261031-235938-d9be7b97), including all 64 original captured frames at exact263..1897 with guard254..1906. All 56 new paused frames pass separately (20261101-000123-67b138bc), exact275..1884 with recorded guard274..1884. Observer-only replay peaks are0.5358/0.5288ms respectively, excluding I/O; the optional2ms cancellation budget is unchanged. New regressions failed first for weak collars, detached graphics, one-row fringes, false terminal candidates, raised sidebars and warmup provenance/parity.

The startup repair preserves an exact submitted Pending image for one first-Ready unavailable input. Still-Pending inputs render/cache fresh permitted output, so they cannot spend the bridge early;59.94/60 and both input parities are covered. Original selected-image provenance and expiry remain unchanged, post-composed reuse is prohibited, and retained optional fill carries its current native certificate. The cold preparation budget is consistently180seconds; post-ready assertions are not relaxed. The OSD now distinguishes unresolved boundaries from a measurement actually awaiting evaluation.

A matched x64 Release full cold-application-cache qualification is running. No full runtime receipt, new deployment or complete live acceptance is claimed yet. Installed ced983fd remains unchanged. The separate95-frame full-raster V210 dim-edge uncertainty and the reported visual transition flash remain open; a textured-edge fallback requires its own raised/asymmetric-matte controls. Research and design decisions are recorded in docs/SOURCE_BOUNDARY_REVIEW_20261031.md in the source branch.

#### Owned subtitle lookahead: failing-first regressions and native GREEN

The a2326513 cold full run completed all six x64 Release builds, then passed 848/851 Standard tests. Beta was not reached; no full qualification receipt exists. One failure was test-session provenance leaking when a renderer address was reused. Two were real post-preparation startup gaps: queue retirement or picture-snapshot rebinding discarded independently measured future subtitle frames, and completed exact scans could be rejected before the first cue existed.

The repair retains a producer-verified source prefix and recomputes caption confirmation from its pixels. A typed timeline witness allows exact completed scans across verified retirement of older queued frames only. It does not restore native AR authority, copy old votes, renew cue expiry, change source identity, or add confirmation delay. Partial, tracked, borrowed, foreign, discontinuous and stale scans remain rejected. Six new mandatory methods include failing-first window/worker/timeline regressions and fixture isolation; all 843 native methods pass in run 20261101-003338-3bd4726e, including all 64 original captured frames with zero boundary mismatches. Independent final wiring review found no additional blocker.

The source is frozen for six focused actual renderer checks across Standard and Beta. This is ongoing partial validation, not full qualification. Installed ced983fd remains unchanged. The separate 95-frame dim full-raster uncertainty and reported transition flash remain open. All five requested Luna xhigh research reviews are complete; robust spatial evidence, existing lookahead and pixel-validated known/remembered geometry are the agreed direction.

#### Three-line output oracle exposes late-ready lookahead loss

Source candidate42ce1661 is committed and pushed. The native ownership controls pass, and moving large nested subtitle buffers to heap storage removes the stack-overflow abort in the six-case actual renderer rerun. The new three-line oracle initially exposed two harness schema assumptions: Emit callbacks have no recorder sequence, and submitted-output records do not populate timeline policy/continuity. The corrected harness uses mutex-ordered callback ordinals and the exact selected analysis-input context, with native controls for the real zero-field schema. No subtitle/source assertions were relaxed.

Corrected actual diagnostic evidence against unchanged matched42ce Release binaries passes3/6: Beta Generated and both Black cases pass; Standard Generated composes only2of3persistent lines, and both Unknown-input fixtures expose a post-ready startup gap. In the partial-line trace, future24 completes before source22 is rendered, but source22 still consumes an earlier two-frame preview; the next source uses the longer window and accepts all3lines. This is concrete missing-ready-evidence behavior, not a reason to lower detection thresholds or extend cue expiry.

A nonblocking completed-results harvest after native lookahead and before dequeue is being implemented through the existing owned-window producer. It must add no wait, new scan or worker submission, preserve a richer valid window, and retain all identity/domain/context checks. Full cold-cache and dual GPU qualification remain pending. The installed ced983fd build is unchanged; no new manual deployment, merge or release is claimed. DraftPR243 records these limits. Test-only diagnostics now rebuild the small native harness (~15seconds) and use the supported explicit plugin-root override against unchanged Release DLLs; those runs remain clearly partial evidence.

#### Completed-result harvest, bounded raw history and duplicate-scan repair

Current source d57a5ec9304f3a682b2924a45f6a26ab12327575 is committed and pushed on codex/native-boundary-cadence-20261031. All 850 mandatory native contracts pass in 20261101-014730-4c362151. The two supplied native boundary sets remain 64/64 and 56/56 passing; no pixel-replay claim is made for the separate 95-frame dim full-raster uncertainty.

Actual renderer checks drove the next repairs. Late completed-result harvest improved the six-case matrix to 4/6 at d4e08b4c. Bounded original raw history preserved the existing four-source three-line proof and improved it to 5/6 at ceb7e211 (20261101-013042-c237da79). The remaining Beta Generated startup gap followed queue recovery into an unfinished scan; the trace also showed two concurrent scans of one source after its picture-plane snapshot changed. No queue-policy relaxation, extra AR wait, ratio snapping or extended cue expiry was introduced.

The final worker change coalesces active/pending raw work only for exact source/settings/policy/continuity. Original job ownership remains immutable. A result crossing from unavailable to available picture geometry must be conclusive and independent; incomplete results cause a miss and allow the canonical scan. Complete caption absence remains reusable, and same-key partial observations remain observable. Valid-domain duplicate-work tests failed first, and the wider matrix exposed and reproduced the retry-suppression gap before its repair. Coverage includes 27 work-key mismatches and 15 completed-result cases.

Matched x64 Release actual renderer validation is running for d57a5ec9. Full cold-cache and required dual-dependency GPU/cue qualification remain pending. No new deployment or full receipt is claimed; installed ced983fd remains unchanged. The reported visual transition flash and separate dim-edge uncertainty remain open. All five requested Luna xhigh research passes are complete; the generic spatial/lookahead/pixel-validated-prior design is documented in the source review. Draft PR243 remains in progress.

#### Cold-cache first-Ready repair and faster GPU correctness fixtures

Clean d57a5ec9 passed all six actual renderer cases and 217 Standard plus 217 Beta GPU/cue methods. Full fresh-cache qualification20261101-014305-7648c242 passed863/864 Standard; Beta was not reached. The remaining Unknown-input startup failure had a complete raw three-line scan but insufficient real-source cue acquisition. Worker completion must not stand in for presentation readiness.

Current47ea8de3 is committed and pushed. An eligible first-Ready warmup bridge tests acquisition on a heap tracker copy. Only after a successful original-image repeat can authentic current raw pixels enter bounded raw history. Repeating an image adds no evidence or lease renewal. Exact-current proven AR changes veto this fallback. No four-source threshold relaxation or additional AR delay was introduced. Two regressions fail before repair; all854 native methods pass in20261101-015836-746d5486. Full cold qualification is running before another broad GPU run.

The test-only correctness fixture now shares bounded compiled-program bytes while recreating all GPU/frame resources. Dedicated cold-shader tests keep independent caches. The prior broad GPU baseline was24.1/24.4minutes Standard/Beta; the new reuse/pixel assertion and speed improvement remain unmeasured until the next run. Installedced983fd remains unchanged. All95 dim captures were additionally measured: left-terminal votes remain9..11 below12 in every frame; no qualified left-edge prior exists in this sample. Dim-edge uncertainty and visual transition flash remain open.

#### Faithful capture tests and late raw-source retention

Candidate a1a7189207c33d3b3d867b22ac2f1b9bfffe9379 is committed and pushed. All 859 native contracts pass in 20261101-024510-29ee90a7. Four new regressions fail first in 20261101-023636-2ae75ea2: late completed raw predecessors, changed duplicate completion order, current four-source acquisition and withdrawal from both raw-history paths.

The test harness previously copied its entire growing trace each input. During long cold shader startup, nominal 59.94 Hz capture slowed to about 38 Hz and legitimately exceeded the retained-image age bound. Incremental trace processing and exact-source proof indexing preserve every assertion and original callback ordinal. The corrected long cold Unknown test passes at measured 59.9417 Hz over 5,719 inputs, including the deliberate repeat and fresh successor. Separate first-Ready failures persisted at faithful input rates; traces prove all three predecessor scans completed before current consumption but were unavailable to confirmation.

A bounded raw-only archive now retains those genuine measurements separately from current READY admission. Current-ready expiry is unchanged. Original job revisions, changed-interpretation tombstones, strict source/ink/context checks and atomic replacement of both raw-history caches prevent stale resurrection. Current pixels still satisfy the existing four-source glyph/backing proof; old observations grant no native AR, accepted cue, lease renewal or repeated-image vote. No extra runtime wait or timing grace was added.

Test iteration also improves: the Standard shader correctness class passes 24 cases in 209.63 seconds versus the prior 23-case method sum of 1,026.68 seconds, approximately 4.9 times faster. Fresh-device program-reuse/pixel probes pass on both dependencies; dedicated cold Motion caches remain private. Full-suite timing is still pending.

Matched Release cold six-case validation is running, followed by required broad dual-dependency GPU/cue and full qualification. Installed ced983fd remains unchanged; no new deployment or full receipt is claimed. The 120 supplied boundary replays remain passing evidence; the separate 95-frame dim-edge ambiguity and unverified visual transition flash remain open. Draft PR243 is updated with these limits.


## Typed backlog work and original held-seed repair, 2026-11-01 03:18 EST

Candidate 58ecfb5834cdbdf82dbac5e05480d84c35bf2783 is clean, committed and pushed. All 864 native contracts pass in 20261101-031602-3df6ee2f. Previous a1a71892 cold matrix 20261101-024708-83f08457 finished 5/6; Standard Black 60Hz exposed duplicate 21/22/23 work after queue continuity 6 to 7 (about 82ms CPU) and held 22/23 clearing genuine seed 21. Typed queue ownership now coalesces only exact-source work, with strict completion-copy admission and canonical retry for incomplete results. Held current cue proof preserves original source/samples/expiry only when backing and native context agree. RED evidence 030420-33678f75 plus independent expanded-backing RED 031424-c352073e; no increased expiry, extra runtime wait or AR threshold relaxation.

Matched Release cold-six is running (58ecfb58-cold-six.log). Required broad GPU and final full qualification remain pending. Installed ced983fd unchanged. Five Luna reviews complete; native boundary 120/120 passes remain scoped replay evidence. Separate dim-edge uncertainty and visual flash remain open.


#### Verified manual-review deployment, 2026-11-01 03:24 EST

58ecfb5834cdbdf82dbac5e05480d84c35bf2783 passes all864 native and all6 cold actual-renderer cases (20261101-031736-ed67be6b). Both59.94/60Hz persistent-caption and original whole-frame repeat paths pass. It is now deployed for the user's explicitly authorized manual review: PID48576, matching x64Release host/Standard/Beta hashes, no configuration edits, previous runtime and user settings backed up under C:\Videoprocessor\vp\backups\manual-boundary-cadence-20261101-032344. Full qualification remains Pending, accurately recorded in manual-deployment-58ecfb58.json. Required broad dual-dependency GPU/cue suite58ecfb58-motion-both is running, followed by finalfullqualification. No merge/release publication or complete live acceptance is claimed. Dim-left-edge uncertainty and visual transition flash remain open.


## Fully qualified deployed candidate, 2026-11-01

Candidate58ecfb5834cdbdf82dbac5e05480d84c35bf2783 passes final full qualification20261101-034810-784f5d9b:878/878 Standard and878/878 Beta, each864native plus14actual presentation methods. tools/assert_runtime_qualification.ps1 succeeds against exact clean source fingerprint3ea3dba59a521e3d535ee975a7f2f10335292b47e3e90cd671dc660d70f3b399. Required broadGPU suites pass220/220 each. All217 previous broad methods are retained, three added, dependency hashes unchanged. Standard fullGPU time falls24.1 to11.5minutes; Beta24.4 to11.1minutes. Dedicated cold preparation remains exercised.

All21 installed runtime EXE/DLL files match the qualified build; PID48576 is running C:\Videoprocessor\vp\VideoProcessor.exe. No second restart was needed. Configuration was preserved, previous build/settings backed up under C:\Videoprocessor\vp\backups\manual-boundary-cadence-20261101-032344. Initial manual deployment accurately remains Pending-at-deployment; manual-deployment-58ecfb58-qualified.json and backup post-deployment-qualification.json record subsequent Passed qualification. No merge or release publication.

Five Luna xhigh reviews are complete. Generic spatial border detection applies acrossSDR/HDR/DV; source-pixel measurements are separate from preservation margins. Existing lookahead and known/remembered hypotheses add no fixed real-time vote/wait or standard-ratio snapping. Supplied64+56 native boundary replays pass, with respective measured source Y263..1897 and275..1884 at3840x2160. This is replay evidence, not complete live acceptance.

Remaining: manual playback assessment, reported visual AR-transition flash, and dim-edge uncertainty. The95 V210 capture contains top/bottom raster-terminal picture witnesses, not proven letterbox crop edges; it starts about19seconds after the screenshot and cannot reproduce the earlier visible border state. Sampled video timestamps decode black. Partial-axis crop authority and expanded remembered-family policies remain separate tested-design follow-ups, not additions to58ecfb58. Both stories remain InProgress; draftPR243 records current scope and limits.


## Native scope/menu detection repair deployed, 2026-11-01 08:22 EST

Commit 25a6abfd6cdc848a0380e71f4338d9b0479caa0f is clean, committed and pushed
on the existing draft PR243 branch. Current paused scope pixels measure
[0,263,3840,1897], or 3840/1634 = 2.3500611995:1. All 92 moving Netflix menu
frames measure full [0,0,3840,2160]. This fixes actual current source-pixel
observation; a held presentation ratio is not the success criterion.

The scope veto incorrectly treated a smooth interior scene gradient as an
additional matte boundary before fine verification. Full-raster picture contact
also added its contrast to a discard threshold that already included a margin,
instead of the actual black floor. Independent broad exterior picture evidence
now rejects false navigation-panel borders. Structured footer/card evidence
must also pass independent source-floor terminal support; a patterned true
matte cannot bypass faint first-picture protection. No standard-ratio snap,
platform special case, added temporal wait, or subtitle-path edit is included.

Five mandatory detector regressions raise the native minimum to 869. All 869
pass, with failing-first scope/menu and patterned-matte evidence preserved.
All 371 supplied native frames pass: menu92, current scope64, original DV64,
earlier paused DV56, and full-raster sun/star95. The former dim95 capture is
now a passing full-picture positive; it remains temporally distinct from the
screenshot taken earlier. Generic tests include Full/Limited levels, all four
directions, smooth gradients, weak collars, raised matte and localized UI.

Fresh full qualification 20261101-081446-816fd7db passes 883/883 Standard and
883/883 Beta (869 native plus14 actual presentation methods each). The exact
source/binary assertion passed immediately before deployment. All21 installed
runtime EXE/DLL hashes match. VP was closed at07:33 and remains closed: this
build is ready for the next user launch, not a claimed live playback result.
No configuration edits; saved Classic subtitles and other user settings were
preserved. Previous matched runtime and settings backup:
C:\Videoprocessor\vp\backups\manual-boundary-cadence-20261101-082235

Important retained limitation: initial full run20261101-075900-b72e2d57 passed
882/883 Standard and failed Black subtitle startup at60Hz source18 (Beta was
not started). A fixed five-pair same-harness/private-cache comparison reproduced
the same clipping assertion on unchanged installed58ecfb58:4/5 passed, while
candidate25 passed5/5. Native boundaries stayed correct. This establishes an
existing startup/current-analysis gap after one legitimate repeat, not its
absence or a precise regression-rate comparison. The later green run does not
erase this failure. No test was weakened or removed. Proposed ownership-checked
raw prefetch admission across queue-only retirement is archived for a separate
failing-first follow-up; no implementation is included here.

Evidence: E:/codex/diagnostics/netflix-basic-detection-20261101, including
runtime-Release-x64-25a6abfd.json, deployment-25a6abfd.json,
guarded-capture-replay-summary.json, paired-black-startup/comparison.json and
existing-black-startup-gap.md. BroadGPU220/220 results remain previous58ecfb58
coverage, not a new run. Current observer-only repair reran the full actual
presentation matrix on both dependencies. Manual AR-transition flash and full
live acceptance remain open; stories stay InProgress. No merge/public release.


## Design-first review after new live failures, 2026-11-01

User confirmed the ten core source-picture requirements and explicitly requested
review/redesign before further implementation. Runtime/source remains25a6abfd;
no production edits, rebuild or deployment in this review. The previous371
captured-frame passes and883/883 per-renderer qualification do not cover the new
failures or establish complete live acceptance.

New grid capture082835 reproduces1476 false strong [136,332,3840,2160]
observations despite58,914 real header pixels above332. Sparse fixed probes miss
avatar/logo components. New scope capture083046 contains15 byte-identical paused
frames: true263..1897 bottom is missed by nomination and spatial support rules;
fine search instead visits1814..1838 around interior1826. Both are independently
measured source-pixel failures. The grid's exactly black outer layout margins
need explicit full-canvas-versus-matte inference, not a forced16:9 label.

Three independent reviewers audited geometry/OSS, capture conversion and
lookahead/cadence. Current raw crop-guard detector and optional physical/OSD
detector have conflicting geometry ownership; queued/live confirmation also
needs consolidation. Preserve canonical source adapters, exact ownership,
calibrated range, conservative discard checks and working subtitle machinery.
Proposed flow: shared spatial evidence and competing candidates, global picture/
matte reconciliation, one logical source-timeline decision owner, and separate
measurement/uncertainty, crop permission and rendering outputs. No extra dwell,
standard-ratio snapping, whole-input fallback masquerading as detection, or
new subtitle lease/cleanup authority. Explicit missing/partial/contradicted
outcomes and bounded speculative evaluation are required.

Primary FFmpeg, mpv and HandBrake sources were compared. Their useful spatial
techniques do not establish live HDMI transition correctness; timing/aggregation
policies are not adopted blindly. No new scaling/unpacking defect was found on
reviewed paths. All13 existing selected CPU conversion/parity contracts ran and
passed, exact method set verified, no DV AVX skip reported. These methods are
not all in the current critical gate: add the cheap conversion subset during
implementation. Paired live raw-tunnel/decoded data remains a specific gap if
conversion becomes suspect; arbitrary extra captures are not required now.

[Reviewed design and rule disposition](../assets/VP-0222/ar-design-review-20261101/design-review.md)
includes independent review appendices, source locations, preserved/replaced
rules, outcome/authority contracts, staged migration and acceptance. Prototype
choices (feature coverage, scoring, full-canvas criteria and numeric live shadow
budgets) remain to be measured; live shadow cannot be enabled before budget
selection/testing. Next deliverable is a failing-first CPU prototype/corpus and
deterministic first-source timing replay, not another threshold patch. Genuine
scope-to-IMAX-and-back full-rate transition capture is needed for final dynamic
validation because current diagnostics subsample native frames. Existing Black
startup gap remains separate. Keep story InProgress and PR243 draft.

## First redesign implementation stage (2026-11-01)

The new CPU prototype measures native edges with explicit sampling coverage and uncertainty, separates glyphs from picture support, rejects interior child rectangles using exterior occupancy, and requires independent evidence for canvas inference. It preserves odd/asymmetric/nonstandard boundaries. An existing-worker shadow path is off by default and bounded to one admission per 500 ms with a 12 ms cooperative budget; it cannot publish AR or crop permission.

Ready native evidence now survives expired scan budgets and compatible backlog without a new scan, dispatch or vote from a repeated source. Review also reproduced a subtitle proof handoff hole: four-source raw history could accept all three lines while the caller's short grouping still contained two. The tracker now exports its existing exact-current acquisition certificate for that strong case. Pending recovery continues to validate current pixels, ownership and original age; held/partial/weak cues cannot mint or renew the seed. Bounded diagnostics expose the seed disposition.

Final clean source and both renderer dependencies passed 924 required tests each. Three fresh-process, empty-cache beta repetitions passed the previously intermittent method at both 59.94/60 Hz. Earlier candidate failures remain in the linked evidence, including a post-Ready queue-gap loss; the deterministic seed defect is established, but exact causality for that historic failure is not overclaimed. All new contracts and the 13 conversion/parity checks are mandatory. The qualified-build reuse switch removes redundant rebuilding from future broad GPU runs while preserving hashes and execution checks.

The 479-instance native replay has zero mismatches; the newest 15 instances are one repeated paused frame. These results establish this stage, not final live scope/IMAX performance. The next work is the typed geometry/retention/discard interface and one timeline owner, then bounded every-source transition evidence. No runtime/configuration deployment or source integration merge occurred. Existing unrelated tracker changes were preserved.


## Active spatial and subtitle repair (2026-11-01)

The native capture reproduced caption-panel boundary rejection, circular current glyph/AR verification, and an identical-plane handoff that discarded completed subtitle work. Repairs preserve physical source geometry while using current owned caption pixels only to verify the otherwise occluded bars. Outward retention of filtering fringes does not change the measured AR. Validated native sampling removes repeated layout checks without altering conversion values or coverage.

Additional failing-first tests cover default zero-lead physical reporting, final-image retention from actually verified pending composition, and immediate initial acquisition from source-bound spatial and discarded-band proof. Plain odd boundaries retain their original nomination separately from chroma-aligned crop bounds. Legacy startup and established transition rules are unchanged. The complete build evidence, rejected/stale/malformed controls, saved-capture results, backups and live verification are in the linked stage2 record.

An earlier GPU run was contaminated by a relaunched elevated VP instance; its failures remain recorded and are not attributed to a code change without isolated evidence. The player was subsequently force-closed with explicit user authorization for clean validation. No speculative queue scheduling exception was added. Experimental active mode is selected by the Spatial Test launcher; ordinary default-mode qualification is not represented as universal active-mode acceptance.

## Paused boundaries and dark raster repair (2026-11-01)

The paused1.9:1 native capture repeats four ±4-code pixel phases while black bars remain at64. One phase split boundary contrast across adjacent rows; stacked legacy admission gates then blocked even verified expansion. Bounded spatial evidence and a single temporal owner now preserve native measured coordinates without forcing standard ratios. The sun capture independently exposed an unqualified perimeter percentile suppressing dark picture contacts. Current distributed spatial continuity resolves that case with explicit raised-matte, faint-collar and sparse-widget negatives. Fresh subtitle acceptance now takes precedence over a redundant weaker local hold through the ordinary existing acceptance rules.

Provisional b21ef205 passed unrestricted replays but failed the live24ms worker deadline. It was superseded, not accepted as fixed. Final9cbcf47b adds exact sample reuse and stops interior searches after a qualified outer contact wins. All957 native contracts pass; all94 paused frames and97 sun frames pass the actual bounded worker; six real Classic/Black/transition methods pass across both shipped dependencies. Live post-startup reports48/48 measured per interval with zero unknown/budget overruns and roughly4–5ms average analysis. The recorded parallel-compiler stress misses and diagnostic-writer fixture failure remain preserved.

The operator explicitly requested a quick local review build while broader analysis continues. Matching x64 Release host and both renderers are backed up, hash-verified and deployed with active spatial mode; configuration is unchanged. This is focused manual-review evidence, not full cold/GPU qualification or final visual acceptance. Story remains InProgress. See [stage3 evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage3.json).

## Outward recovery crop jitter repair (2026-11-01)

Manual review of 9cbcf47b found a separate logic regression: at 16:52, recovery alternated a provisional top10 crop with full-raster presentation, moving the picture and OSD by about7 output pixels. The source analysis met its deadline. Broad picture occupancy outside an accepted crop was being treated as permission to discard the remaining provisional margins. This path originated in 19c46962 and was exposed only after an existing presentation had been admitted; startup's independent visible-pixel veto masked an incomplete first reproducer.

Feature commit a1c05cb32b61723e3c9e15477f27d1e801823dd7 separates current verified discard permission from outward occupancy. The producer recomputes it on each source and may use the exact current subtitle-qualified spatial certificate. Recovery may reveal the entire raster immediately, but cannot apply a cropped target without this separate permission. No dwell, aspect snapping, shader changes, subtitle lifetime changes or measurement threshold changes were introduced.

A producer-backed regression first failed on the unchanged implementation, reproducing admitted top4 despite exact visible picture at top0. The repaired sequence, stale controls, odd physical edge/verified guard, immediate full-raster expansion and real crossing-caption mask/strip-resampling integration all pass. All962 mandatory CPU contracts passed; five new methods are mandatory in the build gate. The tested content was committed unchanged afterwards. No runtime deployment or GPU tests were performed, respecting the operator's viewing session. The later16:53–16:55 unknown measurements had stable output geometry and remain a separate unresolved measurement issue; this repair is not proof of correct detection for that content. Story remains InProgress.

See [stage4 evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage4.json).
## Stable reported detection through small variations (2026-11-01)

The operator explicitly requested that a few percent of variation retain the existing detection value, including when only reporting detection. Feature commit d985c21fee92f5cbedf58e9287d5669f3abde145 implements an inclusive3% deadband anchored to the last accepted measured value. Unknown, unavailable, provisional and canvas-only observations retain that separate estimate. A qualifying measurement outside the band updates immediately; there is no additional frame-count or time dwell, averaging, standard-aspect snapping or cumulative anchor drift. New source generations and explicit detection clear reset the history. The OSD uses a fixed stable label with the anchor's own bounds and provenance.

Raw per-source measurements and successfully submitted picture geometry remain exact and independent; the retained value does not authorize cropping. Logs expose both raw and stable values. Review found that the renderer publishes Unknown before completing measurement for the same source, so ordering permits its first qualified completion immediately while rejecting older sources and repeated qualified reinterpretations. A second failing-first regression proves that path, including consuming a qualification that stays within the deadband. The shared snapshot layout bumps renderer plugin API34 to35; any future deployment must build the matching host and both renderer DLLs.

All975 mandatory CPU contracts pass, including four new detection-history regressions and added existing telemetry/cache/API checks. The initial three tests failed on the old implementation, and the same-source completion regression failed before its ordering repair. The final11 focused checks and complete975 CPU suite passed on unchanged content committed afterwards. No deployment, player restart, GPU execution or configuration change occurred. This fulfills reported-detection stability; it does not claim that the underlying strict boundary verifier has been relaxed or that every previous unknown native measurement now passes crop verification. Story remains InProgress.

See [stage5 evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage5.json).
Display follow-up: per operator request, commit85b6dd4f formats detected AR to two decimal places (for example2.35:1 and1.90:1). Raw and stable numeric values, diagnostics and the3% tolerance retain full internal precision. All18 focused CPU display/telemetry regressions passed in source run20261101-191413-3dd54b5f. No deployment or configuration change.
## Local deployment of jitter guard and stable detection (2026-11-01)

The operator explicitly authorized deploying and restarting even if running. Clean feature commit 85b6dd4f966a7b832415ce5488ff31a8118a175e was rebuilt as matching x64 Release host and both renderer DLLs. All 15 focused checks passed with the deployed spatial detector enabled (VP_SOURCE_PICTURE_DETECTOR=1, shadow=0): nine CPU contracts plus both renderer dependencies' real transition, Classic and Black persistent-caption cadence checks. Source run20261101-192752-79e5758f contains the successful TRX and copied transition logs. The two-decimal display, anchored 3% detection reporting and independent verified-discard jitter guard are included.

A preceding default legacy-mode run20261101-192040-b4ea6c6e passed13/15 but failed both returning-scope checks. This failure is preserved, not waived as a passing result. Independent review identified a legacy near-black recovery loop: a16-consecutive-source requirement resets on retired/skipped source IDs, leaving the prior taller presentation while raw scope is detected. Previous passing run162316 used spatial mode, as proved by guarded active-spatial preflight logs; it is not an equivalent legacy baseline. This is an unresolved legacy defect, not established regression causality from the new crop guard. No assertion or production threshold was weakened. Follow-up needs a lifecycle regression distinguishing witnessed skipped inputs from contradictory measurements.

The manual-review deployment uses the passing active-spatial evidence and is not fully qualified. Full qualification assertion reported missing qualification as expected; no receipt was fabricated. Backup: C:\Videoprocessor\vp\backups\detection-85b6dd4f-20261101-192928. All deployed host/renderer hashes match the build; active cfg/state were backed up and preserved byte-for-byte, with no configuration edits. VP restarted asPID59412 with spatial mode enabled. Startup log confirms clean85b6dd4f host and both plugins API35; process responds and selected DeckLink capture starts, but no input frames or rendering graph were observed during startup verification. Live playback acceptance remains pending. Story remains InProgress.

See [deployment evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage6-deployment.json).

## Verified legacy return without a frame-count hold (2026-11-01)

At the operator's request, feature commit c4c915360e1572650d6b3e85681f8b9a3690bf86 repairs the specific legacy scope → dark → taller → scope failure. The previous installed85b6dd4f runtime reproduces both renderer failures against the same harness: measured2.352941, submitted1.904762. The episode's source-rate recovery quota resets on skipped input IDs, while its own suppression prevents ordinary reacquisition. A later unknown-only presentation resolver also restored the old taller output after the first episode-exit repair.

Independently verified current native entry geometry plus current safe excluded bands now releases the retained episode immediately, without a source-count quota or suppressed-model association. A proof bound to native bounds/source generation/source sequence/presentation epoch reaches the final resolver on that same source. Caption ownership and current visibility/contradiction branches retain priority. No new crop authority, standard-ratio snapping, measurement threshold, subtitle lease or full-raster recovery change is introduced. Weaker/dark/sparse recovery remains separate; this is not a claim that all historical temporal rules were removed.

Two new regressions first failed with production unchanged. All978 mandatory CPU contracts then passed on final unchanged content before commit, including three mandatory new methods, 27 producer negatives and five handoff negatives. Clean committed x64 Release runtime checks passed5/5 in a fresh legacy process (three CPU plus both actual transitions) and4/4 in a separate spatial process (both actual transitions and both Classic persistent-caption cases). Both legacy renderer traces prove source111 was detected and submitted as2.352941 on111, with episode proof0/0. Actual detector-origin assertions and recorded startup settings prevent conflating legacy and spatial evidence; an attempted in-process environment override was discarded because the renderer caches a CRT setting at startup.

Broader Black/reconstruction tests exposed an existing queue-gap/output-safety defect. On unchanged parent85b6dd4f, both Black and Beta Generated methods fail while Standard Generated passes; the earlier new-build spatial run failed all four, without executing the new episode-exit path. This establishes the pre-existing failure class, not universal attribution of every timing-dependent instance. All failed runs and before/after runtime hashes are preserved; no assertion was weakened, no repeated-until-green qualification is claimed, and those modes remain unresolved. Full runtime qualification is not claimed.

The existing installed85b6dd4f player was restored after isolated GPU checks (PID34692); configuration was not edited. The new fix is committed and pushed, not deployed or merged to beta. Story remains InProgress. See [stage7 evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage7-legacy-recovery.json).

## Deployment of verified native-entry recovery (2026-11-01 20:04)

After explicit operator deployment authorization, clean c4c915360e1572650d6b3e85681f8b9a3690bf86 x64 Release host and both renderer DLLs were deployed to C:\Videoprocessor\vp. The exact source identity, both passing legacy/spatial receipts, test result hashes, five successful build stages, current output inventory and Release metadata were revalidated before installation. The full-qualification assertion remains unsatisfied; this is the explicitly authorized local manual-review deployment with known pre-existing Black/reconstruction queue-gap failures disclosed, not a fully qualified release.

Backup: C:\Videoprocessor\vp\backups\native-recovery-c4c91536-20261101-200412. All deployed hashes match their tested build artifacts. Active cfg/state were backed up and preserved without edits; unchanged dependencies and shader caches were preserved. VP restarted asPID61872 with active spatial mode and shadow disabled. The startup log confirms clean c4c91536 host, both API35 plugins loaded and DeckLink Quad HDMI Recorder(2) capture starting; the process responds. No incoming input frames were observed during startup verification, so live visual acceptance remains pending. Story remains InProgress; no beta merge or release packaging performed.

See [deployment record](../assets/VP-0222/ar-design-review-20261101/deployment-c4c91536.json).


## Fresh native boundary investigation (2026-11-01)

Independently queried the current beta and started a clean E: source worktree at
remote a591ad3b, then fast-forwarded cumulative deployed c4c91536 history to
preserve subtitle/runtime dependencies. New commit aee2c7df is pushed on
codex/native-boundary-investigation-20261101. No beta merge or deployment.

Production-context replay reproduced legacy physical-reporting Unknown on the
bright TV capture. Fixed reference holdouts crossed the seven-row dark margin
into picture, while terminal fallback required picture at that dark raster edge.
The correction fits references outside measured shallow candidates and retains
existing continuation/contradiction proofs and faint-prefix vetoes. P010/422
chroma footprints are handled explicitly. No temporal rule or crop verifier was
relaxed. OSD now distinguishes current measured/Unknown, retained estimate and
applied AR while preserving existing retention policy.

All981 CPU contracts pass. Independently decoded57+93 native frames reproduce
exact[0,7..3840,2160] under the2ms production observer budget, with zero mismatches
and peaks0.559/0.462ms.1596 production luma samples match independent v210 unpacking.
The existing spatial bounded worker already measures this boundary on57 frames
while its crop verification remains provisional; normal startup selected legacy.
Earlier1.96 logs prove reporting/crop authority divergence only; matching source
pixels and the true geometry of that event remain unproven.

Separate measurement and pipeline reviewers found no final CPU-scope blocker.
GPU integration, OSD screen fit, source-inset overlay and full-rate dynamic
transition validation remain pending; full qualification is not claimed. Playback
was not stopped or restarted by this investigation. Runtime logs record an orderly
keyboard-close shutdown at20:42:05; VP was left closed. No runtime/config edits.
Story remains InProgress; unrelated dirty VP-0207 design asset is preserved.

See [stage8 evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage8-native-boundary.json)
and source docs/SOURCE_BOUNDARY_INVESTIGATION_20261101.md for reproduction and
bounded remaining work.


## Authorized deployment of source-margin measurement (2026-11-01 20:55)

After the operator requested deployment, clean aee2c7dfe0ff47aa9372b79c6afc200440f36e34
was built as a matching x64 Release host and both renderer DLLs. Five build stages
succeeded. Focused checks passed7/7: three native geometry/OSD controls plus actual
AR transitions and persistent Classic captions through both renderers. A fresh
spatial-detector process passed both additional actual transition methods2/2.
The prior full981 native contracts and150 exact native replay frames remain the
measurement evidence. This is an explicitly requested local manual-review deployment;
full qualification remains unsatisfied and separate Black/reconstruction failures
are still disclosed. No beta integration merge or release packaging occurred.

Backed up and deployed to C:\Videoprocessor\vp. Backup:
C:\Videoprocessor\vp\backups\native-boundary-aee2c7df-20261101-205511.
All deployed runtime/dependency hashes match build artifacts. Both active cfg/state
hashes remain identical, including after launch; no configuration edits were made.
VP is running asPID28524 in ordinary legacy mode. Startup logs verify the clean
commit in both host and renderer and59.94Hz native input. Live physical observations
now measure[0,7..3840,2160],1.783558: the first120 observations were strong, Unknown0,
budget exhaustions0, peak0.761ms. Applied AR remains separately reported16:9.
This confirms the reproduced thin-margin reporting repair on current live content,
not complete scope/IMAX transition or earlier1.96-event qualification.

[Deployment receipt](../assets/VP-0222/ar-design-review-20261101/deployment-aee2c7df.json).
Story remains InProgress; unrelated VP-0207 design changes were preserved.


## Paused source coherence redesign (2026-11-01)

The operator reported a paused scope scene that sometimes detected and required a
review of the agreed design rather than another observer exception. The64 captured
native frames are byte-identical; independent source boundaries are0,263–3840,1897
(2.350061). Legacy replay fails spatially even without a budget limit. Earlier
alternating successful log entries lack matching captured pixels, so no claim is
made to have reproduced every historical flicker.

Pushed source2dc6ca0217be23dd9e4362dec86ea8818038c770 consolidates production geometry
under the common source worker, including converted-P010 analysis. It separates
geometry, completion and crop permission; shares only completed exact-byte source
facts; makes raw publication monotonic; keys derived retention by completed-result
revision; and removes competing retention/subtitle padding rules. Late current
caption proof replaces one provisional evaluation without another vote or an older
source rewinding newer state. Perpendicular native boundary facts now prevent a
confident wrong inset when coarse topology misses broad dim picture.

All1,002 native contracts pass. The current paused capture passes64/64 exact
measurements and64/64 current-caption crop checks. Saved corpus regression passes
479 instances; representative native profiles establish the older paused56 set's
different275–1884 bounds, and its initial wrong-hypothesis failure remains recorded.
The prior bright seven-row margin passes150/150 exact bounded replays. No private
pixels are committed. Exact source and evidence are in the [stage9 record](../assets/VP-0222/ar-design-review-20261101/implementation-stage9-coherent-source.json).

Matching clean-commit x64 Release host/both renderer qualification is running.
No runtime/configuration deployment, beta merge or release packaging has occurred
in this repair. Full qualification and live acceptance are not claimed. Story stays
InProgress; the unrelated dirty VP-0207 design asset remains untouched.

### Runtime follow-up

Full clean2dc qualification completed all six matching x64 Release builds but
failed4/1017 standard tests (1,013 passed). Production-mode and both-bar UI fixture
expectations were corrected without relaxing pixel preservation. Focused follow-up
passed7/9, including both renderer AR transition and both-bar UI checks. The two
remaining no-lookahead Classic translation failures persist in diagnostic run
20261101-234143-c37b0ac7. Its sampled ingress records show both jobs dispatched
while sources remain owned; asynchronous results usually complete after selection.
A bounded current-source preparation handoff is in progress. Source identity,
original translated pose and24 distinct presented-source checks remain required.
The earlier Black/reconstruction failures pass on the new runtime. No qualification
receipt or new deployment is claimed. Stage9 evidence retains failed run locations.

### Source preparation repaired; final qualification running (2026-11-02)

Pushed clean8b49f560e1607be0f2136f25b598ea8dfbec69ec serializes exact native-to-subtitle
preparation and gives the zero-lookahead queue head one enqueue-derived processing
interval. It uses lifetime-owned completion notifications with a revision predicate,
not timer polling; source identity and original pose/24-source checks remain strict.
The notification-focused run passes7/7. Actual standard and beta renderers retain
the translated/fill pose across124 and123 distinct presented sources respectively.
Sampled preparation takes7.5-8.2ms against16.683ms; this is not a general latency
promise. The final pin also wakes from common outward raw publication and adds its
deterministic race test. Mandatory native count is1,008. Full matching Release
qualification and the two-dependency motion suite remain pending, with no deployment.
See [stage10 evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage10-preparation.json).

### Native geometry and current output visibility (2026-11-02)

Clean source0c2919f6bbbe345e8f8e2596df4d3da13f4fde9a restores parallel queued glyph
preparation while retaining bounded ordered preparation at zero lookahead. Advanced
mode no longer authorizes clipping untranslated raw captions merely from requested
mode and accepted native bounds. Missing current composition preserves current raw
visibility or full raster, while native geometry stays unchanged. Current composition
restores native presentation immediately; optional fill cannot trim the safety
aperture. Current visible pixels defeat contradictory absence claims.

The native suite passed1,009/1,010 before correcting one fixture's omitted renderer
admission bridge; that exact initialization check now passes with all same-source
and wrong-context rejections retained. Both Black persistent-caption and both
zero-lookahead renderer checks pass. One beta repeat iteration showed correct repeat
but two subsequent source discards; the unchanged test later passes both59.94/60Hz
with additional queue/timing diagnostics. The failure remains recorded, and no queue
policy, source cadence or renderer assertion was weakened. Full qualification on the
new clean pin, motion coverage and final private replay are pending. No deployment.
[Stage11 evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage11-visibility.json).

### Qualified coherent measurement deployment (2026-11-02)

Source `14da4730` separates current successful caption composition from the stronger
proof that remaining source bands are clean. Exact composition may retain the
measured native viewport without granting cleanup, erasure, initialization or caption
lease authority. Missing/stale composition retains current raw visibility. This
resolves the four residual-UI renderer failures at `0c2919f6` without weakening
existing renderer assertions.

Final qualification passed all 1,012 native contracts and 15 standard renderer
scenarios (1,027 total), plus all 15 beta renderer scenarios. The additional seven
GPU/cue classes passed 220 tests on each dependency. Final bounded replay of the
64 byte-identical paused source captures measured `[0,263,3840,1897]` on every
frame, with zero mismatches and current caption verification on all 64.

The matching x64 Release host and both renderers were deployed and all 28 destination
hashes verified. Configuration/state are unchanged. Backup is
`C:\Videoprocessor\vp\backups\coherent-14da4730-20261102-011508`.
The runtime was launched and both plugins loaded. Current input format differs from
the saved paused scene; window capture timed out, so live visual acceptance of that
original scene remains unconfirmed. No source playback controls were used.

[Final evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage12-qualified.json).

### Presentation observability first (2026-11-02)

The user reported another shrink during normal 3 Body Problem scope playback with
reconstruction captions, then directed work toward retrospective logging rather
than repeated deep frame inspection. Preserved logs show accepted native geometry
unchanged while the submitted aperture jumps to full raster and then wider raw
visibility. A legacy-only report records 61 submitted-geometry changes; missing
caption/preparation facts remain explicitly unknown.

Qualified source `4ec63744` adds opt-in, source-bound presentation decision JSON to
the existing enhanced logger. Each selected packet carries measured/native/visible
bounds, caption currentness, scan/composition/preparation/ownership/continuation
results and final decision reason into the actual submitted output. Changes log
immediately; stable state has a one-second heartbeat. Cadence repeats retain their
original displayed-source facts. Missing identities are explicit. Screenshot
sidecars carry the same decision/output identity. The timeline tool decodes these
records and flags malformed records and reported logger loss, without source pixels.

All 1,015 native contracts and 16 standard renderer scenarios passed (1,031 total),
as did all 16 beta scenarios and two additional normal-mode opt-out checks. The
fully matching Release host and both renderers were backed up and deployed, with
all 28 hashes verified. Added only `[logging] debug: true`; profile state is
unchanged. Backup: `C:\Videoprocessor\vp\backups\telemetry-4ec63744-20261102-080502`.
Live verification parsed 56 records with exact displayed-source facts, 22 state
changes and 34 heartbeats; zero malformed records, reported drops or truncation.
No computer input automation or native source frame capture was used in this work.
The underlying AR/reconstruction defect remains open; this is observability work.

[Evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage13-telemetry.json).

### Recording correlation and automatic incident collection (2026-11-02)

The user requested enough telemetry and screenshots to turn visible failures into
regressions, then requested a visible recording timecode. Runtime `9cd3e5a2` adds
UTC plus instance/generation/output/source IDs to the video and exact per-output
logs when enabled. Requested/effective mode and shader preparation state/key are
recorded. The late timecode pass uses a separate renderer and leaves the cached
movie image clean so repeated outputs get fresh timestamps.

Suspected aperture widening, measured/accepted geometry disagreement or automatic
advanced composition loss may save a failure image and a two-second follow-up,
with up to512 prior decisions. Readback uses DONOTFLUSH/DONOTWAIT, at most2 pending
copies and one writer; captures have a20-second cooldown and a12-image session
limit. Images are at most1920pixels wide and never capture native input frames.
The report tool can look up the entire visible code and index incident images.
Tool-only commit `62d28af2` separates correlation rows from actual decision changes;
it lives in an isolated worktree and leaves the qualified runtime pin unchanged.

All1,019 native contracts and16 standard scenarios passed (1,035total), followed
by all16 beta scenarios, in controlled qualification without a concurrent live
VP renderer. Earlier same-pin runs under concurrent VP load failed a180-second
shader preparation deadline, and one also failed initial Generated caption viewport
coverage. A single recorded compile pass took94seconds; the exact case passed in
isolation. Failures are retained; causation is unproven and no assertions or
budgets were loosened. The targeted Config recording controls and report checks
pass. A broader Config test stops at the unrelated existing tab-sizing assertion.

The matching Release host, both renderer variants, Config app and discovery helper
were backed up, deployed and all33 file hashes verified. Only the two requested
logging options were added to the active configuration; selected profiles are
preserved. Backup: `C:\Videoprocessor\vp\backups\recording-9cd3e5a2-20261102-093158`.
Live verification parsed8,582 records without malformed entries or reported loss;
`14:35:00.042Z 123AA9E6 G1 O10352 S10358` resolves to exactly one submitted output
with current facts and the panel rendered. Automatic capture is armed. The current
input is a near-black/full-raster title state, so no incident image has yet been
observed. A fresh reproduction was requested. The underlying AR defect remains open.

[Recording evidence](../assets/VP-0222/ar-design-review-20261101/implementation-stage14-recording.json).
