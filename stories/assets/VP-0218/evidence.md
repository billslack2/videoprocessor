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

The user completed **Capture analysis diagnostics with input frames...** at 2026-10-26 23:24:51. Capture `C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1` contains 97 native V210/Limited payloads plus structured decisions and presentation records. The recording is complete; the example need not remain paused. See [native capture analysis](native-capture-20261026.md) for exact measurements, evidence limits, artifacts and design corrections.

All 97 payloads replay as `PROVISIONAL` through unchanged beta production evidence code: only the lower bar is trusted. The upper band contains a gray gradient and texture that fail discovery and excluded-band safety despite strong local Y/U/V change. The recorded model already has 255 confirmations, and all 1,516 presentation records retain the full raster; no near-black or timer gate explains this example. The initial uniform, equal-matte proposal is insufficient and has been explicitly revised with a separate, unqualified variable-profile experiment.

A diagnostic 302-to-1858 crop was used only to interrogate existing safety checks; it was never established geometry. Row299 already contains red chroma, so it is not a clean exterior sample and the 299-to-304 boundary contrast does not identify a safe final crop row. Preserve transition uncertainty and independently annotate the desired crop before qualifying a new route.

Native payloads span approximately 1.852 seconds with 15 unsaved source positions after reaching the 2 GiB frame budget; metadata covers a longer interval. Native recording gaps are not playback drops. The paused capture proves persistent spatial rejection, not the original transition onset or improved switching latency. Original frames, decoded planes, preview and full runtime logs remain local. The archived artifact set contains probe source, numeric results, provenance/hashes and structured summaries only.

## Tracker audit and preservation

The pre-assignment audit found 236 canonical records and 236 indexed entries, maximum/root registry217 and next218; no duplicate/missing/orphan/noncontiguous IDs or registry count conflict. [Audit details](tracker-audit-before.json).

The [native-capture update audit](tracker-audit-native-capture.json) again finds 237 canonical/indexed records, last root218 and next219, with no new registry or state issue.

Pre-existing state discrepancies were reported and deliberately left unchanged: VP-0088 status capitalization; VP-0168 index/status capitalization; VP-0211 in-progress folder/status versus Review index; and VP-0070 lacking the standard Status heading. This review does not silently repair or renumber unrelated stories.

The dedicated tracker already had an unrelated edit in `stories/assets/VP-0207/team-design-20261023/design.txt`. It is preserved. The new design is committed from a clean main-based tracker checkout under E:\codex and synchronized to origin/main; synchronization must preserve that pre-existing edit rather than claim the dedicated worktree is clean.

## Remaining verification

The earlier design review resolved the queued-consumer, raw-to-qualified provenance and shared-owner findings. The later native capture added a distinct variable-exterior problem, exact-edge uncertainty and absolute-visibility reconciliation requirements; these are now explicit qualification work, not solved behavior. Final independent review confirmed the six new safeguards, then requested two wording/coverage corrections: forbid fitting specifically to pass this image, and distinguish qualifying raw provisional axes from challenging a magnitude-derived full-raster measurement. Both corrections are incorporated in the design. No production fix, full VP build, native/GPU regression run, live acceptance or deployment occurred in this design review. Required next evidence is specified in the design: annotated native positives including actual moving transitions, independent adversarial negatives, complete authority/retention integration, source-frame latency through actual submission and bounded analysis cost.
