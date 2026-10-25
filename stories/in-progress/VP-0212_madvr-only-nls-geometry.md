# VP-0212: Use madVR crop feedback exclusively for madVR NLS

## Status

In Progress

2026-10-25: User authorized story creation and implementation. Current GitHub
beta/default is v1.4.02-beta at b1415da4e2cc12807f390f4b722c459be9a17c08.
Source branch: codex/madvr-nls-source-geometry-20261025.
Worktree: E:\codex\videoprocessor\madvr-nls-source-geometry-20261025.
Canonical origin/main ID audit: 230 files and index rows, no duplicates or
missing entries; maximum root 211. Existing unrelated VP-0207 asset edit is
preserved and excluded from this story's commits.

## Problem

A user reports NLS squashing content already at 2.35:1 when using madVR.
Currently madVR crops bars independently while VP's detector supplies the NLS
rectangle; unavailable VP detection falls back to the complete raster. Two
geometry authorities can disagree and stretch the wrong active area.

## Scope

Use IMadVRInfo originalVideoSize and videoCropRect as the only measured source
geometry for madVR NLS. Validate source coordinates against the negotiated raster,
stabilize changing feedback, and preserve source identity across output DAR
changes. Never derive source aspect from arAdjustedVideoSize or output rectangles,
because NLS itself changes the output DAR. No VP detector or guessed full-raster
fallback when madVR data is missing/invalid. Keep NLS armed but pass through at
native output until current valid feedback is established. Restore native output
and remove nonlinear mapping when feedback is lost. Retain existing target,
tolerance, direction, shader mapping and madVR ownership of bar removal.

VP Renderer detection, rendering, configuration and automatic crop are unchanged.
No deployment, merge or release is included.

## Acceptance criteria

- 2.35:1 source on a matching target passes through; valid narrower source can
  engage NLS according to the configured direction/tolerance/stretch limit.
- Actual madVR source-space bounds, including asymmetric bars and pillarboxing,
  feed both eligibility and shader coordinates.
- Missing interface, failed queries, invalid/empty/out-of-range or mismatched
  rectangles suspend NLS without consulting VP's detector or inventing a crop.
- Changes/loss withdraw stale geometry promptly; reacquisition is bounded and
  tied to the current renderer/source. Output DAR changes cannot feed back as
  source aspect changes or cause restart oscillation.
- Focused native tests cover matching scope, narrower/wider source, malformed
  feedback, transitions, renderer replacement and output-aspect independence.
- Successful x64 Release build and test evidence recorded before Review.
- Live madVR acceptance remains explicit if hardware playback is not exercised.

## Validation plan

Test the production geometry policy and existing shader/runtime regressions.
Build the complete x64 Release solution. Review the diff for any VP Renderer
changes. Record madVR's detection-setting dependency and confidence limits;
IMadVRInfo does not expose a detector confidence or frame timestamp.
