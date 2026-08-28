# VP-0156: Eliminate transient four-sided bars during live Screen and Zoom profile changes

## Status

In Progress (2026-08-27). Implementation base `v1.3.001-beta` was freshly
discovered from `billslack2/videoprocessor` and explicitly confirmed by the
developer. Work is on `codex/vp-0156-profile-switch-geometry` in clean
worktree `C:\\Videoprocessor\\vp\\vprenderer\\.codex-worktrees\\vp0156`.

Readiness review: deployed logs reproduce the defect and show a viewport
request clearing trusted active-picture geometry immediately before the
transient frame. The source format and source pixels do not change during a
profile selection. The implementation boundary is known: carry only current
trusted source-picture geometry into the new presentation epoch, while
subtitle evidence, translation, hold timing, envelope, and fit state reset and
reacquire. Focused native tests and an x64 Release build can validate the
change.

Tracker audit: after fetching `origin/main`, 174 canonical records and 174
index rows were found, with no duplicate IDs or registry mismatch. `VP-0155`
was the highest root ID, so `VP-0156` is the next safe assignment.

## User story

As a VideoProcessor operator changing Screen or Zoom profiles during live
video, I want the new presentation to begin with the already trusted source
picture geometry so no intermediate frame is fitted from the encoded raster
and surrounded by black on all four sides.

## Root cause

Every applied application profile advances the viewport request serial. The
render thread treats that serial as a new active-picture authority epoch and
clears trusted source geometry. Its first frame therefore fits the full encoded
raster into the selected screen. Current-frame analysis restores the trusted
bar crop on the following frame, causing the visible one-frame flash.

## Acceptance criteria

1. A live Screen profile change applies the new destination geometry using the
   last current, trusted source-picture bounds on its first rendered frame.
2. A Zoom-only profile change does not transiently fall back to full-raster
   fitting when the source and trusted bounds remain current.
3. Carryover is allowed only across the same transport/source-format/render
   generation and only for trusted geometry; provisional, unavailable, stale,
   or contradictory evidence is never promoted.
4. The first current frame verifies retained geometry and may immediately
   withdraw it when excluded-band pixels are unsafe or the source changed.
5. Subtitle state does not cross the profile boundary: subtitle evidence,
   translation confirmation, fit confirmation, hold/release timing, overlay
   envelope, and presentation evidence reset and reacquire normally.
6. NLS presentation intent is recalculated for the newly selected target; no
   old target aspect, stretch decision, or shader hook crosses the boundary.
7. Source discontinuities, format changes, renderer rebuilds, and transport
   generation changes retain their existing full invalidation behavior.
8. Focused regression tests and a clean x64 Release build pass.

## Boundaries

- Do not alter subtitle placement, detection, timing, or smoothing policy.
- Do not preserve subtitle-derived source translations or overlay envelopes.
- Do not change madVR behavior or DirectShow renderer behavior.
- Do not suppress genuine source/format/renderer invalidation.

## Evidence
