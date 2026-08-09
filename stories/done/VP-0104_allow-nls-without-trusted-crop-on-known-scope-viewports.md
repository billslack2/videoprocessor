# VP-0104: Allow NLS without trusted crop on known scope viewports

## Status

Done (2026-08-09). The focused repair is implemented as `0d2b7db`, built,
deployed, and accepted in live madVR/projector use. It is merged and pushed to
both requested beta branches: `9eeaa74` on `v1.2.001-beta` and `79e0c83` on
`v1.2.001-formats-test-beta`.

## Completion checkpoint (2026-08-09)

- Source commit `0d2b7db` is pushed on
  `origin/codex/v1.2.001-live-config`.
- Merge commit `9eeaa74` is the verified remote tip of the default
  `origin/v1.2.001-beta` branch.
- Merge commit `79e0c83` is the verified remote tip of
  `origin/v1.2.001-formats-test-beta`.
- Both remote tips contain `0d2b7db` as an ancestor.
- A fresh serial x64 Release solution build passed on the default-beta merge.
  The clean worktree used the established shared Qt 6.8.3 dependency root for
  the standalone configuration-editor projects.
- The focused `Vp0099ProvisionalCropFallsBackToNumericRasterGeometry` test
  passed from the default-beta merge.
- The deployed formats-beta Release build and its unchanged active
  configuration passed the user's live CIH projector validation.

## User story

As a CIH projector user switching to a numerically defined scope viewport, I
want NLS to engage from the known source raster and physical screen geometry
even while active-picture crop evidence is provisional, so the image expands
horizontally instead of remaining in `NLS: Waiting` or being vertically
squashed.

## Problem and root cause

After recent viewport and crop-authority changes, the Alpha renderer required
a trusted crop rectangle before constructing its final NLS mapping. During a
live renderer/viewport switch, a provisional crop candidate could therefore
veto NLS even though VP already knew both relevant geometries: a 3840x2160
16:9 source raster and a configured 47:20 scope viewport. The viewport's name
is only a label and must not be used to infer geometry.

madVR exposed the same state-selection weakness as `NLS: Waiting` after the
switch, while its native HLSL path also made the monitor preview look vertically
compressed. On the actual CIH projector, the known numeric viewport geometry
is sufficient and the accepted result is horizontal expansion.

## Implementation

- Resolve NLS source geometry from a trusted applied crop when one exists.
- Otherwise fall back to the complete numeric source raster rather than
  treating a provisional crop candidate as authority or a veto.
- Feed the same resolved geometry into Alpha's final NLS mapping.
- Add a regression test for a provisional `76,0-3840,1968` crop candidate on
  a 3840x2160 raster mapped to a 47:20 viewport. The expected result is active
  horizontal NLS with a 1.321875 stretch, not `Waiting`.

## Verification

- Full x64 Release solution build passed serially.
- Focused VP-0104 regression coverage passed.
- Full native test assembly: 723 of 728 passed. The five failures are the
  existing configuration/reference failures also present before this repair.
- Release binaries were deployed together without changing active
  configuration; the prior binaries were backed up under
  `C:\Videoprocessor\vp\backups\before-nls-raster-fallback-20260809-030657`.
- Live madVR/projector validation passed: after viewport switching, NLS starts
  and the CIH projector presentation widens as expected.

## Acceptance criteria

- NLS activation is derived from numeric raster and viewport geometry, never
  from a viewport label.
- A provisional or absent crop does not block safe full-raster NLS.
- A trusted applied crop remains authoritative when available.
- Viewport switching does not retain a stale NLS `Waiting` decision when the
  full source raster and numeric viewport geometry are known.
- The x64 Release build, focused regression test, and live CIH projector test
  pass.

## Integration targets

- `v1.2.001-beta` (the repository default branch discovered 2026-08-09).
- `v1.2.001-formats-test-beta` (the second user-requested 1.2 beta branch).
