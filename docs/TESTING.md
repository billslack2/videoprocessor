# Test gates for VideoProcessor

Use the smallest appropriate gate while iterating, then qualify the exact final
source and binaries before release. A smoke pass is not release qualification.
All commands below run from the selected source worktree. Build and test x64
Release; never deploy Debug. Do not run test builds concurrently in one worktree
or stack GPU suites on the same machine when measuring their duration.

| Stage | Required checks | What it establishes |
| --- | --- | --- |
| Development iteration | Native Smoke plus the regression for the changed behavior | Fast feedback on common breakage; no deployment approval |
| Requested local trial deployment | Successful complete x64 Release solution build; Smoke; affected-area regressions below; Config checks if changed; test the exact final source | Suitable for an explicitly requested trial, with remaining release coverage stated |
| Final build, final deployment, installer or portable distribution | Full native suite; full Config suite; beta parameter checks; both renderer load orders; updater helpers; affected-area regressions; package checks for packaged artifacts | Automated qualification of the final candidate; live and installer acceptance remain separate |

If impact is unclear, shared pipeline/lifetime/threading code changes, or multiple
areas interact, run Full before a trial too. After an authorized deployment,
verify both host and renderer hashes from the same build, preserve configuration,
and check startup, capture, renderer selection, playback and the changed behavior.
For a final deployment, complete automated qualification before replacing files.

## Fast native smoke (target: under 2 minutes; hard test-session limit: 5 minutes)

```powershell
.\tools\test_vp.ps1
.\tools\test_vp.ps1 -List
# Repeat the same smoke against the other shipped dependency when relevant:
.\tools\test_vp.ps1 -Suite Smoke -Variant beta
```

The runner incrementally builds the native test project, both renderer plugins and their dependencies with
Release/x64. A fresh checkout still has compilation cost; subsequent invocations
reuse MSBuild outputs. It does not build the complete deployable solution. Visual
Studio is discovered with vswhere, or use `-VsInstallPath`. The workstation's
Magewell SDK default can be overridden with `-MagewellSdkRoot`.

`tools/test-smoke.json` is a reviewed allowlist of whole native classes plus nine
individual GPU regressions from otherwise expensive classes. Coverage includes
configuration parsing/migration/RPC, capture and native formats, Dolby Vision,
source-pixel bar evidence and boundary refinement, subtitle ownership/lookahead,
clocks/queues/recovery, shader parameters, SDR/HDR/LUT readback, NLS and composition.
Whole-class selection includes newly added tests in those classes. Review growth
using `timings.json`; do not silently remove failing or slow tests to get green.

Four opt-in replay diagnostics that return early without private recordings are explicitly excluded from Smoke. Full retains them, but their no-fixture passes do not establish recorded-source coverage. The standard dependency runs by default. Smoke does not replace exhaustive
subtitle GPU, corpus/replay, Config UI, hardware capture, display or package tests.
Detector acceptance is measured source-pixel boundaries and actual picture aspect:
missed bars, false crops, preservation of picture/captions, transition response and
stability. Screen ratio, fitting, CIH, zoom and NLS are downstream checks and must
not determine the expected detected aspect.

Evidence is saved in a fresh `artifacts/tests-<suite>-<id>` folder, or a new
`-OutputDirectory`. Logs, TRX, per-test timings and a JSON receipt include source
commit/fingerprint/dirty state, test DLL/dependency hashes and separate build/test
times. Failure preserves evidence and prevents a success receipt. Empty, failed,
skipped, aborted, missing selections, unexpected selections, changed sources or
changed binaries fail the run. Dirty-source receipts are development evidence;
release acceptance requires a clean pinned source and the matching build receipt.

## Targeted native regressions

```powershell
.\tools\test_vp.ps1 -Suite Targeted -Classes ConfigFileTests,ConfigEditorCoreTests
.\tools\test_vp.ps1 -Suite Targeted -Classes SubtitleBackingLipTests,SubtitleBoxDetectorTests
.\tools\test_vp.ps1 -Suite Full
```

Class names are exact short native class names, not filenames or loose patterns.
A class that does not execute fails the selection check. Full is the unfiltered
native DLL, with no smoke timeout; it still verifies nonempty, all-passed results.
Neither Targeted nor Full automatically runs the non-native checks below.

| Changed area | Additional coverage before deployment |
| --- | --- |
| Bar detection, native analysis, AR transitions, crop decisions | Full native suite (detector policies span many classes); relevant recorded-source reproductions; live bar/picture/caption transition acceptance |
| Subtitle detection/placement, composition, processing, packet lifetime, GPU preparation, smooth motion | Relevant native detector/worker/stack regressions and `tools/test_subtitle_motion_renderers.ps1` against **both** shipped dependencies; relevant recording reproduction and live subtitle/motion checks |
| Capture formats, conversion, Dolby Vision or capture recovery | `MagewellCaptureTests`, `MagewellPackingWorkerTests`, `MagewellRgb24PackingTests`, `NativeP010CaptureTests`, `NativeRgbAnalysisTests`, `VideoFrameFormatterTests`, `DolbyVisionTunnelFormatterTests`, `CaptureResyncHardeningTests`, `InvalidCaptureStateGraceTests`; device/format-change and signal-loss/recovery checks on affected hardware |
| Timing, queues, resets, display rate, fullscreen | Affected DirectShow/clock/queue/reset/display native classes; Full for shared lifetime/threading changes; live refresh/format transition, stutter and recovery checks |
| Shader, LUT, tone mapping, NLS, renderer ABI/dependencies | `LibplaceboLutParserTests`, `LibplaceboRenderParametersTests`, `MotionRendererTests`, affected GPU classes against standard and beta; beta parameter test; both plugin load orders; SDR/HDR visual acceptance |
| Config, profiles, migration or remote control | `ConfigFileTests`, `ConfigEditorCoreTests`, `ConfigurationRpcServerTests`, `QualityProfileMigrationTests`; full Config executable; Config Apply/OK/reopen and relevant local/remote behavior |
| Updater, installer, packaging, toolchain or dependencies | Updater/control helpers; installer support/identity/runtime checks; portable contract plus finished ZIP validation; relevant isolated installer/update lifecycle and clean-Windows tests |

These are minimum area checks, not an automatic changed-file selector. Include the
regression for the actual bug and every affected consumer. Any known failure is a
blocker to final qualification until investigated; retain the failing evidence.

## Final candidate checks

1. Pin a clean final commit and complete the full x64 Release solution build using
   the repository's `AGENTS.md` command and the selected Qt/SDK/toolchain inputs.
2. Run `tools/test_vp.ps1 -Suite Full` (or retain the release builder's equivalent
   unfiltered VSTest TRX for the identical final source and test DLL). Do not repeat
   an identical full run solely because packaging starts; source, binary,
   dependency or test-selection changes invalidate reuse.
3. Run the Config test executable after that solution build:
   `& .\x64\Release\VideoProcessorConfigTests.exe`. Check its exit status and retain
   its output. Run from an isolated working folder so temporary editor state stays
   separate from the user's deployment. Native-window interaction cases require
   a desktop session. An offscreen run is not a substitute for those cases.
   The explicit `--test 'Tone charts real editor updates'` and
   `--test 'Tone charts real configured profiles'` cases are opt-in real-renderer
   checks; run them for tone/chart changes with their required renderer fixtures.
4. Build `src/VideoProcessor-BetaTests/VideoProcessor-BetaTests.vcxproj` with
   Release/x64 and run `x64/Release/vprenderer-beta/VideoProcessorBetaTests.exe`
   alongside the matching bundled `libplacebo-388.dll` from the full solution
   output. This project is not included in the solution.
5. Run `python tools/test_libplacebo_variants.py` and the same command with
   `--beta-first`; run `tools/test_updater.ps1` and `tools/test_update_control.ps1`.
   Check every exit status and retain logs.
6. Add the affected-area checks above. For subtitle/source/motion work use:

   ```powershell
   .\tools\test_subtitle_motion_renderers.ps1 `
     -MagewellSdkRoot 'E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3' `
     -VsInstallPath '<selected Visual Studio installation>' `
     -OutputDirectory '<new evidence directory>'
   ```

7. For distribution, follow `docs/VP-0192_INSTALLER.md` and the VP release workflow:
   installer support/identity/runtime checks, actual payload/source hashes and
   `test_portable_config_contract.ps1`, then `test_portable_config.ps1` against each
   finished ZIP. Keep the final build/export gate intact. The existing release
   wrapper still runs full native tests; this smoke runner does not bypass it.
8. Record isolated fresh install/upgrade/repair/rollback/data preservation and
   clean Windows without Visual Studio, including Config Apply/OK/reopen. State
   any untested hardware/interactive coverage explicitly; helper passes cannot
   establish it. Run updater lifecycle cases when updater/packaging changes.

Use existing passing evidence only when it identifies the same clean final source,
relevant test/runtime binaries, dependencies and complete required selection, and
no intervening environment change invalidates it. Do not infer qualification from
an old nearby commit or modify a release receipt to avoid the full suite.

## Timing baseline and runner checks

The 30 October 2026 rc6 DeckLink release TRX at source `605ec4af39b9` ran 3,538
native tests in 2,412.7 seconds (40.2 minutes). The 77 LibplaceboLutParserTests and
22 SubtitleCutPasteShaderTests accounted for 1,892.1 seconds (31.5 minutes) of
individual test time. The initial smoke selection covered 1,460 of those tests
with 54.1 seconds of individual test time. This is historical selection data,
not a guarantee of wall time or a qualification of the new branch. Build time,
GPU/driver, software rendering and host load affect the measured result. The corrected smoke selection (excluding the four opt-in diagnostics) passed 1,456 tests in 59.8 seconds on the `d8518ef8e2f7` application sources. Both renderer prerequisites were built successfully.

Run `tools/test_test_suite.ps1` after modifying runner/result-validation logic.
Validate the real Smoke run and inspect its receipt too. No full native rerun is
needed merely for documentation or selector changes once the runner's behavior
and selected real tests have been verified; application-source changes still
follow the gates above.
