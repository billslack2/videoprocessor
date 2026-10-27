# VP-0218 review evidence

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

The user reported an example paused during review. The Windows computer-use Node helper exited unexpectedly on both attempts, before a screenshot could be obtained. A contemporaneous log snapshot was saved. The user was directed to VP's system-menu **Capture analysis diagnostics with input frames...**, which records current native inputs without restarting or resetting history. As of the last check during this review, no new native capture directory or start event had appeared. Do not label the existing evening subtitle captures as this example.

A native capture is still needed to measure the actual bars, range/domain and boundary profile. A static paused capture establishes spatial evidence; it cannot by itself validate an entire moving transition, queue scheduling or the original history. Preserve any later capture locally with its real sidecars and session summary, then add range/level and fresh-versus-retained replay results to this record.

## Tracker audit and preservation

The pre-assignment audit found 236 canonical records and 236 indexed entries, maximum/root registry217 and next218; no duplicate/missing/orphan/noncontiguous IDs or registry count conflict. [Audit details](tracker-audit-before.json).

Pre-existing state discrepancies were reported and deliberately left unchanged: VP-0088 status capitalization; VP-0168 index/status capitalization; VP-0211 in-progress folder/status versus Review index; and VP-0070 lacking the standard Status heading. This review does not silently repair or renumber unrelated stories.

The dedicated tracker already had an unrelated edit in `stories/assets/VP-0207/team-design-20261023/design.txt`. It is preserved. The new design is committed from a clean main-based tracker checkout under E:\codex and synchronized to origin/main; synchronization must preserve that pre-existing edit rather than claim the dedicated worktree is clean.

## Remaining verification

The independent design reviewer confirmed that the queued-consumer, raw-to-qualified provenance and shared-owner revisions addressed all three review findings, with no remaining design-review blocker. No production fix, full VP build, native/GPU regression run, live acceptance or deployment occurred in this design review. Required next evidence is specified in the design: native reported content, unchanged-baseline failures, independent adversarial negatives, complete authority/retention integration, source-frame latency through actual submission and bounded analysis cost.
