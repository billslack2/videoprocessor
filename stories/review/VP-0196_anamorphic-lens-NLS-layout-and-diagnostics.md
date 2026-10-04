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