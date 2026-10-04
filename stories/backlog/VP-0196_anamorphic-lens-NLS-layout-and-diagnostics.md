# VP-0196: Correct anamorphic lens layout with NLS and diagnostic telemetry

## Status
Backlog

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
Pending implementation. Remote beta and default branch both v1.3.005-beta at
c84863eaa59e03fe218de824c34b7c0220c4fd87. Standing user instructions authorize
that discovered beta as base.
