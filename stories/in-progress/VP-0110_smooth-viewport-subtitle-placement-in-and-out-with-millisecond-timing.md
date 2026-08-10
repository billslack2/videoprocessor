# VP-0110: Smooth viewport subtitle placement in and out with millisecond timing

## Status

In Progress (2026-08-10). The developer confirmed `v1.2.001-beta` as the
implementation base. Work has started on
`codex/vp-0110-subtitle-placement` in
`C:\Users\bslac\vp\vp-0110-subtitle-placement`, based on
`origin/v1.2.001-beta` at `02a7543`. The current deployed configuration
exposes `subtitle_release_drift_seconds`; the new contract will express both
directions in milliseconds.

Implementation checkpoint (2026-08-10): the isolated source worktree now
parses and publishes `subtitle_engage_drift_ms` and
`subtitle_release_drift_ms`, generalizes the renderer's release-only drift into
one retargetable translation interpolator, and preserves the detector and hold
decision path. The Qt configuration editor exposes both millisecond controls.
Targeted x64 Release builds of VideoProcessor-Test, VP Renderer, and the Qt
configuration editor pass; unit-test execution remains in progress. The
unused legacy-editor source module is explicitly outside this story's scope.

Deployment checkpoint (2026-08-10): rebased source commit `c154917` onto the
then-current `origin/v1.2.001-beta` commit `8218f1a`. Clean x64 Release builds
of the host and VP Renderer succeeded with `VERSION_DIRTY=false`. Deployed the
matching `VideoProcessor.exe` and
`vprenderer\\VideoProcessorVPRenderer.dll` pair to `C:\Videoprocessor\vp` and
verified both deployed SHA-256 hashes match their build artifacts. Backed up
the prior binary pair and active `VideoProcessor.cfg` in
`C:\Videoprocessor\vp\backup-vp0110-20260810-105221`. Preserving all existing
configuration content, the active Scope viewport now has
`subtitle_engage_drift_ms: 0` and `subtitle_release_drift_ms: 1000` in place
of `subtitle_release_drift_seconds: 1`.

Follow-up deployment correction (2026-08-10): the initially deployed host and
renderer pair was correct, but the separately packaged Qt configuration editor
was still its prior build and therefore rejected the new keys. After the user
closed that editor, replaced `VideoProcessorConfig.exe` from the same x64
Release build, verified its SHA-256 hash against the build artifact, and
reopened it for validation.

## User story

As a VideoProcessor user watching scope content with subtitle fitting enabled,
I want the translated viewport to ease into the safe subtitle position and
ease back to its normal position, with both durations configured in
milliseconds, so the motion is deliberate and directly tunable. A duration of
`0` must snap to the target position.

## Scope

1. Replace the seconds-based release setting with
   `subtitle_release_drift_ms` and add the matching
   `subtitle_engage_drift_ms` setting for movement into a newly selected safe
   subtitle position. Both accept an integer millisecond duration, have a
   documented bounded range and default, and use `0` for an immediate snap.
2. Update the unified configuration example and `CONFIGURATION.html` so the
   active viewport variants use the millisecond names and descriptions clearly
   distinguish entering from releasing the translated placement.
3. Change only the interpolation of the already-selected subtitle shift:
   interpolate the current rendered/source-window translation toward the
   existing requested shift while active, and toward zero after the existing
   release condition occurs. A new target or direction change restarts from
   the current displayed shift without a discontinuity unless its configured
   duration is `0`.
4. Preserve subtitle detection, bar analysis, confidence/hysteresis,
   opposite-edge handling, hold/release eligibility, crop authority, and the
   decision of *when* a subtitle placement applies. This story changes only
   how an already-selected shift moves in or out.
5. Add focused tests for zero-duration snapping, nonzero engage and release
   interpolation, retargeting during motion, and unchanged detection/hold
   decisions. Complete a clean x64 Release build and the relevant native test
   suite.

## Acceptance criteria

- Subtitle fitting can visibly and smoothly enter as well as release a selected
  translation.
- `subtitle_engage_drift_ms=0` and `subtitle_release_drift_ms=0` snap to the
  corresponding target position.
- The configuration and documentation contain no active seconds-based setting
  for these two movement durations.
- Existing rules for detecting, holding, changing direction, and releasing
  subtitle placement are behaviorally unchanged.

## Non-goals

- Altering subtitle detection thresholds, OCR, black-bar analysis, selection
  confidence, hold duration, crop logic, or subtitle timing.
- Deploying binaries or changing the active installation configuration.

## Implementation gate

`billslack2/videoprocessor` reported `v1.2.001-beta` as its GitHub default
branch on 2026-08-10, and the developer confirmed it as the implementation
base.

## Related work

- VP-0087: VP-managed subtitle fit with madVR presentation.
- The existing viewport subtitle-fit and `subtitle_release_drift_seconds`
  deployment work.
