# VP-0110: Smooth viewport subtitle placement in and out with millisecond timing

## Status

Backlog (2026-08-10). Created from the request to make subtitle placement
transition slowly into its safe position as well as slowly back out. The
current deployed configuration exposes `subtitle_release_drift_seconds`; the
new contract will express both directions in milliseconds. The required
implementation-base confirmation is pending.

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

`billslack2/videoprocessor` currently reports `v1.2.001-beta` as its GitHub
default branch (queried 2026-08-10). Confirm that branch as the implementation
base, or name a different base, before creating a source feature branch,
worktree, or implementation commit.

## Related work

- VP-0087: VP-managed subtitle fit with madVR presentation.
- The existing viewport subtitle-fit and `subtitle_release_drift_seconds`
  deployment work.
