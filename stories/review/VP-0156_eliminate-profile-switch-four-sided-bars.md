# VP-0156: Eliminate transient four-sided bars during live Screen and Zoom profile changes

## Status

Review (2026-08-27). Implemented from the explicitly confirmed
`v1.3.001-beta` base on `codex/vp-0156-profile-switch-geometry` at source
commit `a031190e`; draft [PR #71](https://github.com/billslack2/videoprocessor/pull/71)
targets `v1.3.001-beta`.

The renderer now retains only generation- and source-format-current trusted
bar geometry across a live profile boundary. If the newly selected profile
uses that geometry, the transition frame is force-analyzed before destination
layout. Provisional, malformed, stale-generation, and stale-format geometry
still fail open. NLS intent and shader bindings are rebuilt for the new target.

Subtitle behavior was not modified. The existing subtitle reset calls still
run before the transition frame, clearing translation/fit confirmations,
drift, hold state, detected extents, and presentation envelopes. The duplicate
same-frame reset was consolidated to the render-thread boundary, but subtitle
detection, motion, smoothing, fit, and release policy code is unchanged.

Validation evidence:

- Focused Alpha source-crop/subtitle policy suite passed 103/103.
- Full native suite passed 942/943. The sole failure is the pre-existing
  `ConfigurationReferenceMatchesPublicFieldInventory` mismatch already
  recorded on the beta line.
- Clean x64 Release rebuilds succeeded for both `VideoProcessor-GUI` and
  `VideoProcessor-VPRenderer` from clean commit `a031190e`.
- The matching Release pair was deployed for operator validation. SHA-256:
  executable `088F723CC3723D1AC7C061F2DF1234AA36E42772172D8304AD0C61ED68A254F0`;
  renderer `5311B43518083B34BB81B6350B0D992F057181816D143253880764E70837A143`.
- The previous pair is backed up at
  `C:\\Videoprocessor\\vp\\backups\\vp0156-profile-switch-bars-20260827-2247`.
  `VideoProcessor.cfg` was unchanged, hashes matched the build artifacts, and
  VideoProcessor restarted successfully.

Remaining acceptance: the developer must repeat live Screen and Zoom profile
switches and confirm both that the four-sided flash is gone and that subtitle

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
