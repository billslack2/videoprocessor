# VP-0203: Load calibration LUT disk changes only on explicit Config Apply

## Status

Review

Implementation is committed and pushed as
`462901a13fbfc294f65c262014834d98d2f6da04` on
`codex/lut-explicit-apply-20261022` in
`E:\codex\videoprocessor\lut-explicit-apply-20261022`.
Draft [PR #191](https://github.com/billslack2/videoprocessor/pull/191) targets
`v1.4.02-beta`, from verified baseline
`fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`. Source is clean. The implementation
is ready for PR review; it is not merged or accepted for live playback yet.

Full x64 Release solution build passed. Native focused tests passed 162/162;
the dedicated seven applied-cache regressions passed 7/7. The Windows Config
suite selected 110 tests: 108 passed and two unrelated UI tests failed. Both
failures reproduce with the same result on clean baseline `fcc33fc`; details
and evidence are below. No LUT-scope regression is attributed to those failures,
and no unrelated UI fix is included.

No deployment, player restart, active configuration edit, or live-input runtime
acceptance is requested or claimed. Merge review and any later authorized live
Apply/OK playback validation remain outstanding.

## Implemented behavior and validation evidence

- Playback no longer polls LUT file versions or hot-reloads edited files.
  Cached parsed and rejected outcomes are reused for routine profile evaluation.
- Explicit Config Apply/OK carries a durable reload serial to the render-owner
  safe point, including unchanged accepted settings and same-path file edits.
  A failed compatible reload retains the valid active LUT until a candidate
  validates; actual disable and incompatible-contract handling remain safe.
- Local clean Apply notifies without writing the configuration. Remote clean
  Apply uses the unchanged RPC candidate. Apply/OK/Cancel and invalid-config
  regression checks passed. Host/plugin API is version 29 for explicit intent.
- Configuration help and focused reload diagnostics describe the new policy.

Validation artifacts are under
`E:\codex\videoprocessor\lut-explicit-apply-20261022\artifacts\vp-0203`:

| Check | Result | Evidence |
| --- | --- | --- |
| Full solution x64 Release build | Passed; 0 errors, 10 warnings | `release-final-build.log` |
| Final ConfigTests-only x64 Release rebuild after fixture edits | Passed | `ui-diagnostic-build.log` |
| Focused native LUT/GPU/output/plugin/startup/OSD suite | 162/162 passed | `native/lut-apply-native.trx`, `native-tests-console.log` |
| Dedicated applied-cache regressions | 7/7 passed | `cache/lut-cache-only.trx`, `cache-tests-console.log` |
| Windows Qt Config suite | 110 selected; 108 passed, 2 baseline-confirmed failures | `ui-windows-final-tests.log` |
| Source identity and consolidated result | Commit `462901a13fbfc294f65c262014834d98d2f6da04` | `validation.json` |

The two UI failures are:

1. `two-column cards share row height`: `Missing card: Display`.
2. `primary discovery selectors open on field click`:
   `Missing control: config.general.fullscreen_monitor_name`.

Both were independently reproduced with `QT_QPA_PLATFORM=windows` against a
successfully built ConfigTests-only x64 Release executable from clean detached
baseline `fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`, without source edits. Each
individual test returned exit code 1. Baseline evidence is under
`E:\codex\videoprocessor\render-thread-audit-20261022\artifacts\vp-0203-baseline`:
`source-identity.txt`, `config-tests-build.log`,
`two-column-cards-baseline-test.log`, `primary-discovery-baseline-test.log`,
and the saved exit-code/validation summary files. Baseline executable SHA-256:
`958D987565A1533CD0621486243DE42FE50114ACE22910DBA16FAC680C508227`.

Reproduction entry points use Visual Studio 18 Professional Developer PowerShell:

```powershell
MSBuild.exe VideoProcessor.sln /t:Build /m /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3
$env:QT_QPA_PLATFORM = 'windows'
.\x64\Release\VideoProcessorConfigTests.exe --test 'two-column cards share row height'
.\x64\Release\VideoProcessorConfigTests.exe --test 'primary discovery selectors open on field click'
```

The native TRX files retain the exact executed test names and outcomes; Windows
suite logs retain each selected test's start and pass/fail result. These are
software validation results. No hardware/live-input timing acceptance follows
from them.

## User story

As a VP user, I want edits to calibration LUT files to take effect when I use
Config Apply or OK, so playback does not poll the filesystem or unexpectedly
parse and replace calibration data while rendering frames.

## Scope and behavior

- Remove automatic calibration-LUT filesystem polling and same-path hot reload
  from the playback/render loop.
- Editing, replacing, deleting, or temporarily making a selected LUT unreadable
  on disk has no effect on the active renderer until explicit Config Apply/OK.
- An accepted explicit Apply/OK must attempt to reload the selected LUT even
  when its path, configuration values, and file size/timestamp are unchanged.
- Parse and validate the candidate before replacing the active LUT. A rejected
  same-path reload under the same calibration contract retains the valid active
  LUT and reports the rejection. Preserve safe existing disable/slot/target and
  incompatible-contract handling; do not apply old bytes under a new contract.
- Startup and an actual configured LUT/target selection still acquire the
  requested resource. This change governs in-place disk edits, not LUT color
  semantics, renderer resource ownership, or unrelated configuration behavior.
- Update configuration help/reference text that currently promises automatic
  same-path LUT detection. Add focused diagnostics for explicit reload outcome
  without introducing per-frame logging.

## Predecessors and boundaries

- [VP-0166](../done/VP-0166_correct-integrated-target-frame-3D-LUT-application.md)
  is accepted historical LUT support. This story supersedes only its requirement
  to check selected-file size/write identity once per second. It does not reopen
  VP-0166 or change the post-DTM calibration order, gamut slot selection, path
  constraints, Cube validation, or safe failure behavior.
- [VP-0188](../done/VP-0188_config-scope-and-source-reload-consistency.md)
  established accepted configuration snapshots and explicit saved-config apply.
  This story closes the separate calibration-resource polling path.
- Shader pipeline preparation, screenshot readback, general logging, and wider
  rendering-thread architecture findings remain outside this implementation.

## Readiness review

Reviewed against remote beta `v1.4.02-beta` at
`fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02` on 2026-10-22. GitHub remote HEAD
also resolves to this beta. The user session's VP defaults and explicit scope
supply the implementation baseline; no historical local source HEAD is used.

- Current calibration settings and target slots are already represented in
  `RendererSettings`. `LoadDisplayLut` validates an off-side candidate, compares
  calibration contract identity, swaps on success, and can retain a compatible
  last-known-good LUT on failure.
- `MaybeReloadDisplayLut` performs the once-per-second file-version probe and
  calls `LoadDisplayLut`; its playback call is the narrow removal target.
- The explicit configuration acceptance path must carry a reload intent through
  unchanged-settings optimization, rather than relying on a changed filename or
  a filesystem timestamp. Existing owner-thread resource lifetime is retained.
- No color-pipeline ordering change, background GPU-context owner, or dependency
  upgrade is required. The implemented explicit Apply bridge extends the
  host/plugin contract to API 29. Focused same-path and lifecycle regressions
  prove the reload policy; actual playback timing is not claimed by static review.
- Implementation worktree is clean at the recorded beta baseline:
  `E:\codex\videoprocessor\lut-explicit-apply-20261022`, branch
  `codex/lut-explicit-apply-20261022`.

## Acceptance and validation

1. A selected valid LUT remains active after an in-place disk edit during
   simulated/production-connected playback; no periodic probe/reload path runs.
2. Apply/OK with otherwise identical accepted settings reloads new valid LUT
   bytes at the same path. Cover unchanged file identity where practical.
3. A malformed, missing, or unreadable same-path candidate on explicit apply
   retains the valid active LUT under the same contract and reports failure.
   Corrected bytes can be adopted on the next explicit apply.
4. Actual enable/disable, slot/target/path selection, startup, and rejection
   semantics remain safe; incompatible calibration contracts do not reuse bytes.
5. Run appropriate focused LUT/configuration regression tests and a successful
   x64 Release build. Record exact source commit, commands, and results before
   moving to Review. Independent code review must cover reload intent delivery
   and the absence of filesystem work from steady playback.
6. No deployment or live player/configuration modification is part of this task.

## Tracker audit

Origin/main `4b63b06a7d2249243024ae63b6f360ff996ba5b3` contained 221 canonical
records and 221 indexed rows: 200 roots and 21 children, highest root VP-0202.
Registry Last VP-0202 / Next VP-0203 / count 221 matched. No duplicate IDs,
missing canonical/index items, orphan children, or non-contiguous child sequences
were found. No existing open task covers this Apply-only LUT policy.

Historical formatting issues are reported without repair: VP-0070 lacks the
exact `## Status` heading; VP-0088 and VP-0168 use `In progress` in status text,
and VP-0168's index row uses the same noncanonical capitalization. Many older
status paragraphs combine the state with narrative. This task changes only
VP-0203 and its registry/index entries; it does not normalize historical records.

Review-transition audit after fetching origin/main `ac858d1f` found 222 canonical
records and 222 index rows (201 roots, 21 children). Registry Last VP-0203 /
Next VP-0204 / count 222 matched; no new duplicate, missing, orphan, child-order,
or semantic state discrepancy was found. Historical formatting exceptions above
remain unchanged. VP-0203's file, exact status, and index row move together.
