# VP-0218 review and implementation evidence

## Executed unchanged-beta characterization

Source: `7394ba420999a8fa708718ee4ce4d87e7459369f` (`v1.4.02-beta`). All 39 requested detector cases executed. This is a characterization of existing failures, not 39 passing correctness tests and not a completed solution.

A standalone x64 optimized executable compiled the original production detector and required dependencies directly, without source changes or stubs. It used installed VS2019 `/O2 /MD /DNDEBUG`; this was not a complete VP solution build or a deployable runtime. Initial linker dependency errors were resolved by including the existing dependencies; the final build succeeded. The source worktree remained clean.

Fixture: 640x360 P210, 48-row opposing uniform bars, uniform bright Y300 picture, neutral U/V512, sharp boundaries. Units are ten-bit analysis codes. The same raw samples were characterized with Full, Limited and Unknown metadata; some low Limited cases are deliberately adversarial superblack values, not assertions about normal broadcast levels.

| Bar codes tested | Full | Limited / Unknown |
| --- | --- | --- |
| 0,24,32,40,64,80 | Trusted bars: top48/bottom312 | Trusted bars: top48/bottom312 |
| 25,31 | Provisional; no acquired bar crop | Trusted full raster; bars missed |
| 81,90,104 | Trusted full raster; bars missed | Trusted bars: top48/bottom312 |
| 105,128 | Trusted full raster; bars missed | Trusted full raster; bars missed |

15 of the 39 cases did not return the authored bar crop; 24 did. This confirms that bright picture and clear borders can fail solely under the selected bar-level/range combinations in this fixture. It does not establish actual YTTV values, every native format, noisy/compressed bars, temporal behavior, retention, or final displayed output.

Reproduction artifacts:

- [Probe source](bar-probe/bar-probe.cpp)
- [Build/run command](bar-probe/run-probe.ps1)
- [39 result rows](bar-probe/results.csv)
- [Run record with compiler, source and hashes](bar-probe/run-record.json)
- [Production source hashes](bar-probe/source-hashes.json)
- [Successful compiler log](bar-probe/build.log)

The run record hashes describe the original scratch artifacts. The archived run script removes trailing blank lines only; its separate archived hash is recorded, and the command body is unchanged. Probe artifacts preserve exact stored line endings in Git. The executable remains local. Its SHA-256 is `19EB9B94921C1A57635A7D1CFA7A69727999130EC6DBE8BE92DA0C43E9D1807B`. Native movie/ad input pixels are not repository fixtures.

## Deployed-source comparison and logs

The active log reports host and renderer source `326012fd1c2f0f038815b98d526da83897972d20`. [Git blob comparison](source-parity.json) confirms eight inspected AR/evidence/model/renderer files are identical at that source and reviewed beta `7394ba42`. This verifies those source files only, not executable hashes, compiler settings or the whole deployment.

Local diagnostic snapshots: `E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\vp-current-snapshot.log` and `paused-example.log`. The first snapshot covers 2026-10-26 22:07:52-22:59:11 and includes several actual rejected/pending/adopted evidence routes. These rate-limited, unlabeled events cannot identify an ad, prove the pixels were matte, or establish continuous transition latency. No raw logs are uploaded with this design.

## Paused example capture

The user completed **Capture analysis diagnostics with input frames...** at 2026-10-26 23:24:51. Capture `C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1` contains 97 native V210/Limited payloads plus structured decisions and presentation records. The recording is complete; the example need not remain paused. See [native capture analysis](native-capture-20261026.md) for exact measurements, evidence limits, artifacts and design corrections.

All 97 payloads replay as `PROVISIONAL` through unchanged beta production evidence code: only the lower bar is trusted. The upper band contains a gray gradient and texture that fail discovery and excluded-band safety despite strong local Y/U/V change. The recorded model already has 255 confirmations, and all 1,516 presentation records retain the full raster; no near-black or timer gate explains this example. The initial uniform, equal-matte proposal is insufficient and has been explicitly revised with a separate, unqualified variable-profile experiment.

A diagnostic 302-to-1858 crop was used only to interrogate existing safety checks; it was never established geometry. Row299 already contains red chroma, so it is not a clean exterior sample and the 299-to-304 boundary contrast does not identify a safe final crop row. Preserve transition uncertainty and independently annotate the desired crop before qualifying a new route.

Native payloads span approximately 1.852 seconds with 15 unsaved source positions after reaching the 2 GiB frame budget; metadata covers a longer interval. Native recording gaps are not playback drops. The paused capture proves persistent spatial rejection, not the original transition onset or improved switching latency. Original frames, decoded planes, preview and full runtime logs remain local. The archived artifact set contains probe source, numeric results, provenance/hashes and structured summaries only.

## Tracker audit and preservation

The pre-assignment audit found 236 canonical records and 236 indexed entries, maximum/root registry217 and next218; no duplicate/missing/orphan/noncontiguous IDs or registry count conflict. [Audit details](tracker-audit-before.json).

The [native-capture update audit](tracker-audit-native-capture.json) again finds 237 canonical/indexed records, last root218 and next219, with no new registry or state issue.

Pre-existing state discrepancies were reported and deliberately left unchanged: VP-0088 status capitalization; VP-0168 index/status capitalization; VP-0211 in-progress folder/status versus Review index; and VP-0070 lacking the standard Status heading. This review does not silently repair or renumber unrelated stories.

The dedicated tracker already had an unrelated edit in `stories/assets/VP-0207/team-design-20261023/design.txt`. It is preserved. The new design is committed from a clean main-based tracker checkout under E:\codex and synchronized to origin/main; synchronization must preserve that pre-existing edit rather than claim the dedicated worktree is clean.

## Design review limits on 2026-10-26

The earlier design review resolved the queued-consumer, raw-to-qualified provenance and shared-owner findings. The later native capture added a distinct variable-exterior problem, exact-edge uncertainty and absolute-visibility reconciliation requirements; these are now explicit qualification work, not solved behavior. Final independent review confirmed the six new safeguards, then requested two wording/coverage corrections: forbid fitting specifically to pass this image, and distinguish qualifying raw provisional axes from challenging a magnitude-derived full-raster measurement. Both corrections are incorporated in the design. No production fix, full VP build, native/GPU regression run, live acceptance or deployment occurred in this design review. Required next evidence is specified in the design: annotated native positives including actual moving transitions, independent adversarial negatives, complete authority/retention integration, source-frame latency through actual submission and bounded analysis cost.

## 2026-10-27 implementation and deployment

Source and review: [PR #213](https://github.com/billslack2/videoprocessor/pull/213), final clean commit `ddf5d666803c29e0bdf7af310963b2695e382bc2`, current beta base `7394ba420999a8fa708718ee4ce4d87e7459369f`. The production implementation was committed as `e211bdb929219ea6e60d2e9390b0cf90cbabef8a`; only two test files changed afterward. The whole previously deployed source tree `326012fd1c2f0f038815b98d526da83897972d20` and beta have identical tree `9262299fab9b24dd336b0e3be330ebbb18dd2ab0`, preserving the prior deployment's profile/render fixes. Source worktree: `E:\codex\videoprocessor\vp-0218-adaptive-ar-20261027`. No merge or installer/public release was performed.

Implemented behavior:

- Full-range uniform bars at codes 25–31 enter the existing strict opposing-matte proof; reference qualification extends through the existing code-104 sample envelope. Cutoff 104, neutral-chroma/dispersion/continuity/whole-band retention checks and Limited/Unknown interpretation remain unchanged. The same 39 synthetic cases improve from 24 to 29 correct crops: exactly Full-range Y25,31,81,90,104 improve. Ten misses remain. This is not a test of 39 actual ads.
- Broad and native distributed live predicates retain separate anchored contiguous runs and common episode/owner provenance. One complete route is required; incomplete routes cannot pool votes, repeats cannot add votes and stale identities cannot rewind state. A native→both→native sequence changes from beta's 1/1/1 to 1/2/3 confirmations. Shared episode age cannot be reset by choosing another route.
- Queued native proof uses the same exact source/context and current-frame checks as broad proof. The three-frame native window reaches the first effective source frame in policy/final-crop fixtures. Progressive suppression rechecks the selected route; subtitle-TRANSLATE retirement stays broad-only. There is no new queue wait/growth or new pixel-scan path for route continuity. Physical source-to-display latency was not measured on a moving movie capture.

[Numeric comparison](implementation-20261027/evidence-summary.json), [uniform results](implementation-20261027/uniform-probe/results.csv), and [common-API route probe](implementation-20261027/route-comparison/README.md) preserve before/after results. The route probe uses synthetic metadata. The final-crop native fixture combines synthetic strict-retention evidence with real bright-pixel retention and does not establish behavior on a captured dark movie transition.

All 97 saved V210/Limited ad frames remain provisional. Detector, edges, identities and strict-profile CSVs are byte-identical to beta; the actual ad is not fixed by this patch. The separate frozen [profile experiment](implementation-20261027/profile-experiment/README.md) independently nominated transitions at 300 and 1856 and retained an eight-row uncertainty fringe, but the upper profile failed held-out residual/connected-component limits. A matched low-slope authored ceiling also passes the synthetic model, so it does not qualify safe variable-profile authority. No such route shipped, no boundary was inferred from opposing-bar symmetry, and no thresholds were tuned to force this image to pass.

### Executed test evidence

| Run | Result | Interpretation |
| --- | --- | --- |
| Unchanged beta full native suite | 3,420/3,422 | Two subtitle GPU assertions failed and reproduced individually. |
| Candidate broad core run | 3,354/3,355 | One diagnostic fixture retained native-support masks from unrelated bright synthetic pixels. |
| Candidate full renderer class | 75/77 | Same two unchanged-beta subtitle failures; no additional failure. |
| Corrected focused AR/diagnostics | 395/395 | Includes strict raised mattes, route identity/owner/geometry, queued consumers and the paired weak-edge positive/negative. |
| Corrected subtitle cases, project-local build | 2/2 | Independent pixel-contract repair before final build. |
| Final clean Release retest | 10/10 | All corrected Fit diagnostics and both subtitle methods, exact final root test DLL. |
| Effective full native inventory | 3,433/3,433 | All beta case names covered, 11 added methods, zero remaining failures after explicit targeted repairs. |

Original failures were not discarded or waived. `RecordedWeakUpperStripAndProvisionalSampleCannotPassShadowProof` now clears the synthetic top native-support masks while retaining the original weak broad metrics and expected rejection. Its new positive explicitly supplies distributed support and verifies the native route. The diagnostic fingerprint now includes those masks.

`BlackPanelCoversLastPictureRowWithSquareLowerCorners` previously expected no effect on the first bar row although the existing shader intentionally overlaps attached backing by two pixels. The corrected fixture retains real-black-bar last-picture-row corner checks and adds gray sentinels: y56–57 are black, y58–60 stay RGB230 at both corner probes. `MeasuredCardEdgesRestoreOutsideSamplingRectangleAtEveryDisplayPadding` now explicitly disables attached extension for its strict seam-restoration contract. All original pixel tolerances are preserved; no subtitle production/header/shader code changed.

[Qualification](implementation-20261027/qualification.json) retains full original run hashes, baseline failure names/errors and final exact-build retest provenance. [Verifier](implementation-20261027/assemble-qualification.py) rejects missing baseline methods, non-Passed effective results, stale/baseline overlays, unbound test assemblies and any production-source change after the broad runs. Final source/configuration/build identity and test-DLL hashes are tied to a prepared invocation and successful completion record. The first preflight timestamp parser rejected .NET's seven-digit precision; support was corrected before the successful preflight and deployment. No deployment happened on that failed preflight.

Optional recorded-capture test bodies without their local environment inputs are not physical playback coverage. GPU timing overlapped between some runs, so those durations are not isolated performance measurements. The final x64 Release solution build passed. The 3,433 inventory refers to the native C++ test DLL, not installer lifecycle or interactive Config-editor tests.

### Deployment and remaining acceptance

At 01:13 EDT the verified x64 Release host and both renderer variants were deployed with matching runtime records/PDBs (nine files). All 18 retained renderer dependency DLLs match the final build. Final executable/renderer source identity is `v1.4.02-beta-ddf5d66`, clean commit `ddf5d666803c29e0bdf7af310963b2695e382bc2`. [Artifact hashes](implementation-20261027/build-artifacts.json) and [deployment receipt](implementation-20261027/deployment-receipt.json) record all identities and before/after/backup checks.

Backup: `C:\Videoprocessor\vp\deployment-backup-vp0218-20261027-011307-ddf5d666`. `VideoProcessor.cfg` and `VideoProcessor.state` were backed up and remained byte-identical; configuration edits: none. VP had closed normally before deployment and no player was started. Runtime initialization and physical playback acceptance therefore remain pending for the user's next launch; deployed/backup file hashes are independently verified.

Three Luna agents reviewed implementation and test conflicts, the native ad/profile evidence, and deployment/rollback handling. The deployment review was a script-logic review before final build; the later successful preflight/receipt records actual readiness and deployment. Historical review notes: [transition review](implementation-20261027/transition-review-final.txt), [deployment review](implementation-20261027/deployment-review-final.txt). The story stays In Progress for the captured variable-profile ad, dark-owner/full-raster work and physical playback acceptance.
