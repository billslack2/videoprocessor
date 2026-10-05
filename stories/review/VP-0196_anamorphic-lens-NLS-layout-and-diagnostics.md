# VP-0196: Correct anamorphic lens layout with NLS and diagnostic telemetry

## Status
Review

Created 2026-10-04. User authorized implementation, tests, an x64 Release build,
and deployment to C:\Videoprocessor\vp for live validation. No beta merge or
GitHub Release publication is requested.

## Problem
VP fits the physical screen aspect into square output pixels before fitting a
lens-compensated picture. This can create an undersized picture with bars on all
four sides. Active NLS returns before lens compensation. Native overlays also
need correct pre-lens proportions. Runtime NLS hook failures can silently disable
the hook while retaining an active-NLS destination rectangle.

## Readiness review
Architect and independent GPT-6 Luna Very High QA reviewed beta c84863ea and the
bundled libplacebo 7.360.1 + VP c646b398 source archive. Both approve the geometry
conditionally on integration and GPU regression coverage. Source and DLL hashes
match 3rdparty/libplacebo/README.txt. MAIN is the cropped RGB intermediate before
final scaling. VP uses pl_render_image, not frame mixing. No dependency upgrade
or shader-formula change is expected. The existing D3D11 coordinate-field test
fixture supports real hook rendering and pixel readback.

## Design
- Resolve physical screen S from explicit screen_aspect, otherwise output aspect P times lens L.
- Fit raster screen S/L into the available output; ordinary/fallback picture fits C/L.
- Active NLS fills that raster screen with unchanged normalized shader formulas;
  NLS decisions/crop limits remain physical C versus S, never C versus S/L.
- Share the physical target across early retained-mode checks and final layout.
  Inferred S must not set configuredScreenActive or enable configured-only policies.
- Preserve source crop/detection authority and burned-in subtitle handling.
- Correct native stats, sweep and profile OSD width by L, preserving output-pixel inset conventions.
- Detect runtime-disabled NLS hooks via pinned libplacebo error API; latch failure,
  reassemble ordinary lens-aware layout/overlays, clear and retry at most once before Present.
- Keep floats through geometry; test libplacebo destination rounding/source adjustment.

## Acceptance criteria
- Matching 2.37037037:1 picture/screen, 4:3 lens and 1920x1080 output fills the raster.
- NLS and NLS+ horizontal/vertical mappings retain physical-screen distortion semantics.
- Lens 1 preserves existing behavior; omitted screen fills output without policy activation.
- Active, passthrough, safe fit, waiting and hook-failure paths all compensate the lens.
- Native overlays retain projected proportions and stay inside the visible picture.
- Tests cover ratio boundaries, odd/fractional crops, alignment/padding, live profile changes,
  source subtitles and legitimate black margins; real libplacebo readback verifies composition.
- Change-only versioned layout telemetry identifies source crop, physical/raster aspects,
  lens ratio, screen/picture rectangles, margins, NLS state and fallback reason. Hook failures
  and recovery emit actionable bounded records. Existing logs suffice without configuration replacement.
- Build/test evidence records exact source/dependency identity. Deploy host and renderer from
  the same successful x64 Release build, back up replaced files, and verify hashes.
- User projector acceptance remains explicitly pending in Review.

## Tracker audit
214 indexed items and canonical records; maximum root 0195. No duplicate IDs,
unindexed records, missing files or registry/count discrepancies. Existing legacy
status prose/capitalization is left unchanged; this record uses the exact state.

## Implementation and validation
Implementation started on codex/vp0196-anamorphic-nls in
E:\codex\videoprocessor\anamorphic-review-20261004. Remote beta and default branch both v1.3.005-beta at
c84863eaa59e03fe218de824c34b7c0220c4fd87. Standing user instructions authorize
that discovered beta as base.
## Implementation complete — 2026-10-04
Source commit: 4980bee36f8b4e6a1994165de272e46443425aef, pushed to
origin/codex/vp0196-anamorphic-nls. Beta integration is unchanged; no merge or
release publication performed.

Implemented shared physical/raster geometry, reciprocal native-overlay width,
physical NLS ratio in early and final policy, runtime disabled-hook detection,
one bounded ordinary-layout retry with overlay rebuild, and selective failure
reset when shader selection changes. Versioned final-layout telemetry reports
source and destination bounds, margins, physical/raster aspects, active hook,
latch, recovery and render result. Layout changes are throttled to four per second
with a ten-second heartbeat for lens users. Support guide:
docs/VP-0196_ANAMORPHIC_DIAGNOSTICS.md (also deployed).

Architect final review identified no further critical source defects, conditional
on recovery readback. Luna Very High QA's main coverage gaps were addressed:
actual per-case PhysicalTarget/EvaluateNlsMapping binding, odd output dimensions,
fractional source/target crops, both NLS shaders and axes, native frame/crop-local
overlays. Runtime failure tests compare the entire recovered image to ordinary
fit, assert black margins, and verify disabled-signature reset. Initial recovery
failures exposed a test-fixture omission: libplacebo requires blit_dst to clear a
texture. Correcting the fixture made strict comparisons pass; production already
checks that capability. No shader or libplacebo DLL modification was needed.

## Build and automated evidence
- Clean source x64 Release solution build: PASS, Visual Studio 18 MSBuild,
  v142 toolset 14.29.30133, Qt 6.8.3. Four existing build/deployment-tool warnings,
  zero errors. The initial incremental test linker encountered LNK1103;
  regenerated test objects/link intermediates resolved it. Failed attempts retained.
- Full C++ suite: 1,893 passed, zero failed (including nine focused anamorphic
  cases, eight newly added). D3D11 WARP uses the actual pinned DLL and shipped GLSL.
- Additional Config UI run: 78 PASS records and three failures in unchanged
  popup/foreground tests: native owner popup association, real configuration
  dropdown persistence, and stable hidden-window reveal foreground. These remain
  an explicit validation limitation; no Config UI code/binary was changed/deployed.
  Physical two-monitor check was skipped on the single-monitor desktop.
- Evidence directory: E:\codex\videoprocessor\anamorphic-review-20261004\artifacts\vp0196
  (build-final.log, full-final.trx, anamorphic-eighth.trx, config-final.log,
  deployment-receipt.json). Earlier failing logs remain there.
- Pinned libplacebo SHA-256:
  2BEFD13B92CCC8C034CD2EBEE6E3BDDC7F8FC1323B135AE005F11506C086C572.
  Deployed DLL and both shipped NLS GLSL hashes match the tested dependencies.

## Deployment and rollback
Deployed 2026-10-04 09:31 EDT to C:\Videoprocessor\vp from the same Release build:
- VideoProcessor.exe (built as VideoProcessor-GUI.exe):
  E4E4BD68F9E93660C2B786E6527A3AD76F8104981F8871BF7D34FF25D75CC18C
- vprenderer\VideoProcessorVPRenderer.dll:
  1393446BAE2BE0A83BA0C772EBBAAAFC37E3A069A433B5A765B6E259190F646C
Both copied hashes verified against their generated runtime receipts. Backup:
C:\Videoprocessor\vp\deployment-backups\vp0196-20261004-093146.
It contains the previous host/renderer pair and receipts, prior configuration and
replaced documentation. Rollback must restore the host and renderer together.
Configuration edits: NONE; VideoProcessor.cfg SHA-256 before/after close and copy
is 6F62749F4A84DAF6AD4E0C805BFEEA359F91BFF58F50241AA74564CB0293F23C.

Restarted existing application; process 90136 responded. Startup logs confirm
clean host identity 4980bee, API 20 renderer plugin loaded and DeckLink capture
started. No rendered frame arrived during smoke inspection, so new live geometry
telemetry and optical/projector correctness are not yet observed. Log:
C:\Videoprocessor\vp\logs\vp.log.

## Remaining review
User should test physical lens/grid proportions, NLS off/on and both variants,
source-edge/subtitle visibility, overlays, alignment and live profile changes.
Set the active Screen profile's physical screen aspect and measured lens ratio;
existing settings were preserved and no lens value was enabled automatically.
Collect startup plus VP anamorphic layout/recovery records and a photograph for
any black-bar report. Keep this story in Review until user projector acceptance;
separately resolve/reproduce the three Config UI foreground test failures before
claiming complete UI qualification.
## Rebased package and deployment — 2026-10-04 15:08 EDT
At user request, fetched remote beta explicitly and rebased onto
982adb0ea309a7cf314ec2c933bd3213153ff649 (v1.3.005-beta, PR #127 Config/RPC work).
Git range-diff confirms the anamorphic patch is unchanged. New clean commit:
0b41c322c79d6d30cbcd99e80741aaa9f7466675, pushed with an explicit lease to
codex/vp0196-anamorphic-nls. Remote beta still matched 982adb0e after deployment.
This is a combined test candidate; no beta merge or public release was performed.

Full x64 Release rebuild and packaging workflow passed. 1,906/1,906 unit tests,
49 installer preservation/recovery helper checks, 12 identity checks, runtime
packaging/signature/hash checks, and 225 actual installer lifecycle checks passed.
Lifecycle tests used the same payload with a separate QA AppId, installation
folder and Start menu group; the user's installation/registration was untouched.
The first QA attempt rolled back when the inherited PowerShell module path hid
Get-FileHash. The repeat with the standard Windows PowerShell module path passed;
both attempt logs are retained. Clean Windows without Visual Studio and physical
multi-monitor/optical qualification remain untested. The previous three Config
foreground UI failures were not rerun as a full UI suite on this rebased build.

Unsigned distribution directory:
E:\codex\releases\VideoProcessor-vp0196-0b41c322c79d-20261004
- VideoProcessorSetup-1.3.005-beta-0b41c322c79d.exe
- VideoProcessor-1.3.005-beta-0b41c322c79d-x64-Portable.zip
- SHA-256 sidecars, INSTALL-MANIFEST.json, installer-build.json,
  validation/release-receipt.json and deployment-receipt.json.
The QA-only installer EXE was excluded from exported artifacts; QA scripts and
logs are retained under validation/isolated-installer-qa.

Stopped VP and the warm/background Config process as requested. Deployed the
verified managed payload, including matching host, renderer, Config and discovery
DLL plus private runtimes/documentation; preserved existing seed files and user
settings. All 68 copied/verified entries match their source hashes. An initial
copy hit a transient locked Config runtime DLL; after the stopped process released
it, copying completed with the original backup preserved and unchanged hashes
skipped. No binaries were launched until the entire deployment verified.
Backup: C:\Videoprocessor\vp\deployment-backups\vp0196-rebased-20261004-150459.
Configuration edits: NONE; hash preservation was checked before restart.

Restarted process 74624 responded. Logs confirm host/renderer clean identity
0b41c322 and successful live 3840x2160 rendering with VP anamorphic layout schema1.
Observed preserved active settings: physical screen 2.35, lens 1.0, NLS waiting
under presentation fail-open. This verifies live telemetry, not physical lens
acceptance. Story remains Review for user testing.

## Separate test deployment — 2026-10-04 17:32 EDT
User corrected the desired location: run separately, not from the main deployment.
Extracted and hash-verified the portable package into
E:\VideoProcessor-Test\VP0196-0b41c322. Copied the user's current configuration,
state, shaders and LUTs into that separate folder; main configuration was not edited.
Restored the 68 backed-up main-deployment entries from
C:\Videoprocessor\vp\deployment-backups\vp0196-rebased-20261004-150459,
verifying each restored hash against that snapshot. Main VP/Config remain stopped.
Only the separate VP (PID 103828) and its separate Config (PID 118496) are running.
Startup confirms host/renderer 0b41c322 and its own log:
E:\VideoProcessor-Test\VP0196-0b41c322\logs\vp.log.
Use the separate executable for further VP-0196 testing. Receipt:
E:\codex\releases\VideoProcessor-vp0196-0b41c322c79d-20261004\standalone-receipt.json.

## Combined Magewell package — 2026-10-05
User authorized including 0b41c322 in the latest Magewell build. The reported log
was dafdba96: active NLS retained identical rectangles for lens 1.0, 2.0 and 1.25,
confirming the missing VP-0196 path. Remote codex/magewell-optional remains
dafdba962487381be87fd04ccf9b07ff1375dbe5 and contributor PR #128 remains
315c0f98f2f7ec2356986fffae96235d738b84f0; no newer remote update was found.

Merged both histories without conflicts in clean commit
18a3ea99da3bc9f3aeccfb2e61e81d892a45a266, pushed on
codex/magewell-anamorphic-20261005. Capture matches dafdba96 and renderer/geometry
tests match 0b41c322. The shared Magewell branch, beta and other local work were
preserved. Full x64 Release build with SDK headers 3.3.1.1596 and all 1,938 native
tests passed. Installer support, identity and runtime packaging checks passed.
Magewell isolation was checked relative to 0b41c322; GUI/Config/discovery have no
vendor load-time imports and no vendor runtime is bundled. Production no-card
probe passed with runtime unload before Stop. Physical Magewell capture, optical
acceptance and clean Windows remain unqualified. Real installer lifecycle was
not rerun with VP/Config sessions active; unchanged packaging previously passed
225 isolated lifecycle checks.

Unsigned, hash-recorded packages and validation:
E:\codex\releases\VideoProcessor-Magewell-Anamorphic-18a3ea99da3b-20261005
- VideoProcessorSetup-1.3.005-beta-18a3ea99da3b.exe
- VideoProcessor-1.3.005-beta-18a3ea99da3b-x64-Portable.zip
- SHA-256 sidecars, validation/release-receipt.json, Magewell check logs,
  TESTING.txt and both testing/diagnostic guides.
No deployment was changed or process stopped. Use a separate portable folder.
Story remains Review for tester validation.

## Updated Magewell rebuild and portable config enforcement — 2026-10-05
User explicitly requested committing the local Magewell updates before rebuilding.
Committed and pushed codex/magewell-optional at
42bb002642eca668beebeaf0c80f88cabfb27023 (RGB12 compatibility conversion,
format rejection, logging/profiling and tests). Merged into the combined branch
codex/magewell-anamorphic-20261005 at
b09988946c1e49086d73c76d7efaefb44d36f28d and pushed. Capture sources match
42bb0026 and renderer sources match 0b41c322 exactly.

The previous portable archive already contained root VideoProcessor.cfg.example
(10,233 bytes). Installer seeds the active name; portable keeps the example name
so upgrades do not overwrite user settings. Packaging commit d78f9d02 now enforces
one nonempty root sample matching source SHA-256 and seed manifest metadata,
rejects an active config, and blocks distribution on failure. Eight fixture
checks cover valid, missing, empty, changed, nested, duplicate, active config
and bad manifest cases. Updated repository AGENTS.md, installer documentation,
and persistent vp-release skill/export workflow. Skill validation passed.

Full x64 Release build succeeded. Initial suite passed 1,942/1,943: the unchanged
DeckLink sustained conversion timing benchmark exceeded p95 during concurrent
probe compilation. Preserved initial evidence; standalone benchmark passed.
Reused verified Release binaries with unchanged hashes/source and reran the full
workflow without concurrent compilation: 1,943/1,943 passed. All eight portable
config checks, actual archive validation, installer support/identity and runtime
packaging checks passed. Magewell PE checks have no vendor load-time imports;
new-change source isolation used d78f9d02 (three Magewell files). Production
no-card probe passed and unloaded runtime before Stop. Hardware capture, optical
acceptance, clean Windows and interactive qualification remain pending; real
installer lifecycle was not rerun. Packages remain unsigned.

Verified packages, hashes and receipts:
E:\codex\releases\VideoProcessor-Magewell-Anamorphic-b09988946c1e-20261005
- VideoProcessorSetup-1.3.005-beta-b09988946c1e.exe
  SHA256 9702721FF89D2DFB31EB65A1E7B1586598E67ABDE584568269F18C615BD97F05
- VideoProcessor-1.3.005-beta-b09988946c1e-x64-Portable.zip
  SHA256 88037FC18A5398FC9AE212788036EA4EE3C08AF12082899704575D11CA96CC41
- validation/release-receipt.json, initial-failed-run evidence, Magewell logs,
  TESTING.txt and capture/anamorphic diagnostic guides.
No deployment or running process changed. Extract into a separate folder;
fresh users may copy the sample to VideoProcessor.cfg, existing users preserve
their own configuration. Story remains Review for tester validation.

## Beta merge — 2026-10-05
User authorized push and merge. PR #130:
https://github.com/billslack2/videoprocessor/pull/130
Merged the tested combined branch b09988946c1e49086d73c76d7efaefb44d36f28d
into the discovered current beta v1.3.005-beta, producing
61fb90ad038e5b9a17c33245c97e2bca42641754. Verified the fetched merged tree is
identical to the tested source (1,943 tests passed). Includes anamorphic/NLS,
optional Magewell updates and portable sample configuration enforcement.
No deployment or new package build performed. Existing b0998894 packages retain
their accurate source identity. Remains Review pending physical tester acceptance.
