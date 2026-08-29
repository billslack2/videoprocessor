# VP-0164: Backdate fully proven inward lookahead transitions

## Status

In Progress (2026-08-29). The authoritative tracker was synchronized with
`origin/main`; its 182 canonical records and index rows matched with no
duplicate or missing IDs, making `VP-0164` the next valid root ID. Source work
uses clean worktree
`C:\Videoprocessor\vp\vprenderer\.codex-worktrees\v1.3.004-nls-timeline`
on remote branch `v1.3.004-beta`, created and published from the verified
remote `v1.3.003-beta` tip
`61869a01cece715c007d93583d323bc1cd1c44d9`.

The implementation is intentionally narrower than the reverted VP-0136 dwell:
it may change only the effective frame association of a transition the existing
model has already confirmed. It may not add a confirmation vote, change
detector cadence, manufacture a decision, or delay a genuine aspect change.

## User story

As a scope-screen operator, I want an already-confirmed same-axis inward crop
to take effect on its first still-buffered frame when every intervening frame
independently proves the exact same safe bars, so an NLS transition does not
show one old-geometry frame at a real scene or aspect change.

## Confirmed live symptom

In the 2026-08-29 Eternals/NLS trace, candidate source sequence `602`, the
cadence-skipped preview frame `603`, and confirmation sequence `604` were all
already buffered and inspected. The new trusted geometry was
`0,276-3840,1884` (2.3881:1), but the conservative outward-only association
rule left the confirmed inward transition on sequence `604`. Sequence `602`
therefore used the prior `0,68-3840,2092` geometry for roughly one frame,
which can look like a brief flash at the NLS transition.

Warm NLS toggles themselves remained fast and had no shader compilation,
restart, fail-open, or queue drop. Off-to-NLS also intentionally changes from
linear pillarbox to a full-width nonlinear mapping; smoothing that geometry
would require a separate crossfade/interpolation design and is not part of
this story.

## Safety design

1. Preserve `ActivePictureTransitionModel` observation cadence and decision
   order exactly. Unscheduled preview evidence is proof/veto data only and is
   never passed to `Observe()`.
2. Retain a bounded exact-identity evidence certificate for every already
   extracted, pending non-repeat preview frame.
3. Preserve the existing outward association rule. Add inward association only
   when the previous and target geometries are valid trusted bar crops, the
   target is strictly contained in the previous bounds, and their same
   non-empty trusted axis set and raster match exactly.
4. Require every accepted identity from candidate through confirmation to
   remain pending and contiguous, share transport/source-format/viewport/
   renderer context, and report the exact same `BAR_CROP_TRUSTED` target bounds.
5. Require evaluated non-near-black evidence on every proof frame. Missing,
   unavailable, provisional, full-raster, near-black, mismatched, consumed,
   discarded, discontinuous, or generation-stale evidence fails closed.
6. Require configured and safely available lookahead to cover the complete
   candidate-to-confirmation span. Any shortfall leaves the decision on its
   confirmation frame and marks it late as today.
7. Retain render-time exact identity, current-frame classification/bounds, and
   transition-adoption validation. A continuity break after publication must
   invalidate a queued decision before it can affect pixels.
8. Log one publication-level association (`outward`, `exact_inward`, or
   `confirmation`) with proof length, without per-frame diagnostic spam.

## Acceptance criteria

- The exact `602`/`603`/`604` replay associates the already-confirmed inward
  transition with `602` only when all three frames prove identical trusted
  top/bottom bars and are still pending.
- Gaps, discarded or consumed frames, insufficient configured or available
  lead, bounds/raster/axis mismatches, provisional/full-raster/unavailable/
  near-black evidence, generation changes, and post-publication continuity
  breaks all retain confirmation-frame behavior.
- Initial full-raster-to-bar acquisition, new-axis or mixed-axis crops, and
  orthogonal nested crops cannot use the inward certificate.
- Lookahead `0` remains behaviorally equivalent; existing outward lookahead
  behavior and transition-model decision order remain unchanged.
- The captured VP-0136 2.2018-to-2.3472-to-2.2018 sequence gains no new
  publication and no new confirmation vote. This story must not recreate the
  reverted eight-second dwell or otherwise change transition acceptance.
- Focused timeline/transition tests, differential lookahead tests, the complete
  x64 Release build, and broad native/configuration test suites pass, with any
  pre-existing baseline failure identified explicitly.

## Adjacent lookahead review

- Increasing the current 24 fps lookahead is not expected to improve this
  replay: four available frames already covered the two-frame proof span.
- The 50/60 fps profiles use lookahead three while the normal detector interval
  is approximately four/five frames. Testing depths four/five later may be
  useful, but only with measured render cost and without increasing queue
  targets or making it part of this correctness fix.
- Preview scene correlation, NLS interpolation/crossfade, and shader prewarming
  are broader designs and remain out of scope.

## Related stories

- VP-0082: Buffered active-picture lookahead for Alpha.
- VP-0124: Safe outward active-picture lookahead.
- VP-0136: Prevent transient same-axis inward aspect switches.
- VP-0163: Prevent full-raster flashes during overlay confirmation.
