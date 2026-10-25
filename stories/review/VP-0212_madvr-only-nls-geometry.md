# VP-0212: Use madVR crop feedback exclusively for madVR NLS

## Status

Review

2026-10-25: Implemented and pushed clean commit
`a5769e53a89c4684104feb90cfedd2de06eff55e` to
[draft PR #200](https://github.com/billslack2/videoprocessor/pull/200).
The complete x64 Release solution build succeeded (22 warnings in existing
code, zero errors). All 3,247 native tests passed in 19.49 minutes, including
eight new production-reader/runtime regressions. Generated host/renderer build
identities are clean at this commit; host, renderer and test-DLL hashes match
normal Release runtime receipts. [Recorded validation](../assets/VP-0212/validation-a5769e53.json).

madVR NLS now reads only IMadVRInfo originalVideoSize/videoCropRect; double
reads reject observed changing feedback and bounds must match the negotiated
raster. Unavailable/invalid data withdraws nonlinear mapping and returns to
native output through the existing coherent transaction. Explicit valid full
raster remains valid madVR feedback. madVR owns temporal detection stabilization;
VP adds no separate classifier or dwell. Source/output DAR feedback is avoided
by excluding AR-adjusted and output rectangles. VP Renderer is unchanged.

Live madVR playback remains the acceptance gate, especially mixed-aspect
content, disabled detection, renderer/source changes and dynamic-DAR rejection
or replacement. IMadVRInfo exposes no detection confidence or frame timestamp;
valid but consistently wrong crop reports cannot be independently corrected.
Optional native replay diagnostics that return early without supplied recordings
are not live playback acceptance. No deployment, active-config edits or merge.

The tested base is b1415da4. Beta advanced during validation to b4d0b5cf via
PR #199 (VP Renderer promotion). Inspected changes have no madVR overlap and
PR #200 is mergeable; its prospective integration tree has not been separately
built. Revalidate exact integration before a later merge/release decision.

Initial build attempt compiled but failed runtime-receipt generation because
Windows PowerShell inherited core PowerShell modules. An isolated child-process
PSModulePath using Windows PowerShell modules resolved it; no source tooling or
machine-wide setting was changed. The successful clean build is authoritative.

Tracker audit: canonical file/index/registry counts agree at 231 with no duplicate
IDs or missing items. Existing VP-0070 uses legacy status prose without an exact
Status heading, but its blocked folder and index agree; preserved rather than
rewriting that unrelated story. The pre-existing VP-0207 asset edit is untouched.

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
reject changing reads, delegate stabilization to madVR, and preserve source identity across output DAR
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
