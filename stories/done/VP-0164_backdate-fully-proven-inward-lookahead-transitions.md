# VP-0164: Backdate fully proven inward lookahead transitions

## Status

Done (2026-08-29). The user accepted the completed implementation and requested
story closure. Implemented and pushed directly on remote beta integration branch
`v1.3.004-beta` at source commit
`66f22307158b2da40f2cdb0ee398ea6ab62c5997`, starting from the verified remote
`v1.3.003-beta` tip `61869a01cece715c007d93583d323bc1cd1c44d9`.
The final clean x64 Release solution build recorded
`VERSION_BRANCH=v1.3.004-beta` and `VERSION_DIRTY=false`.

GitHub has no pull/merge request whose head or base is `v1.3.004-beta`; this
change was already integrated by committing directly to the beta branch, so
there is no separate merge commit to record. Unrelated open draft PRs were left
untouched. Deferred performance and policy experiments are tracked separately
in VP-0165.

The implementation is intentionally narrower than the reverted VP-0136 dwell:
it may change only the effective frame association of a transition the existing
model has already confirmed. It may not add a confirmation vote, change
detector cadence, manufacture a decision, or delay a genuine aspect change.

## Verification evidence

- Independent static safety reviews found no remaining production correctness
  blocker after the axis, continuity, live-policy, queued-reference, and
  generated-pixel-evidence findings were corrected.
- The exact timeline/generated-frame safety matrix passed `38/38` on the final
  clean x64 Release build.
- The broad native suite passed `1000/1000` when the known
  `ConfigurationReferenceMatchesPublicFieldInventory` baseline failure was
  excluded. Running that one check alone fails identically in both this build
  and the exact `v1.3.003-beta` baseline build, so it is not a VP-0164
  regression; this story changed reference prose but not `data-fields`.
- The complete standalone x64 Release Config UI suite passed, including the
  single-monitor synthetic placement fallback.
- The added global near-black proof veto samples at most the existing bounded
  `16 x 16` grid (`256` samples) per trusted-bar preview. No queue target,
  capacity, detector cadence, or wait was added. Live preview-p99 telemetry is
  still required before declaring the older performance gate satisfied.

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
   target is strictly contained in the previous bounds, both use exactly one
   identical trusted axis (`TOP_BOTTOM` or `LEFT_RIGHT`), their raster matches,
   and the two orthogonal coordinates are unchanged.
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
   transition-adoption validation. A continuity boundary at or within the
   effective-to-observation span invalidates the decision permanently; a
   strictly later boundary does not retroactively poison already-proven pixels.
8. Log one publication-level association (`outward`, `exact_inward`, or
   `confirmation`) with proof length, without per-frame diagnostic spam.
9. Scene detection resets ordinary temporal candidates, but it is not positive
   crop authority and is not a blanket veto for `exact_inward`. A scheduled
   decision may cross a detected cut only because every buffered frame,
   including the effective cut frame, independently proves the exact same
   trusted non-near-black bars. This narrow pixel certificate avoids restoring
   the confirmed one-frame flash.

## Acceptance criteria

- The exact `602`/`603`/`604` replay associates the already-confirmed inward
  transition with `602` only when all three frames prove identical trusted
  top/bottom bars and are still pending.
- Gaps, discarded or consumed frames, insufficient configured or available
  lead, bounds/raster/axis mismatches, provisional/full-raster/unavailable/
  near-black evidence, generation changes within the proven span, and
  continuity boundaries at or within that span all retain confirmation-frame
  behavior. Strictly later boundaries preserve the earlier proven decision.
- Initial full-raster-to-bar acquisition, new-axis or mixed-axis crops, and
  orthogonal nested crops cannot use the inward certificate.
- Lookahead `0` remains behaviorally equivalent; existing outward lookahead
  behavior and transition-model decision order remain unchanged.
- The captured VP-0136 2.2018-to-2.3472-to-2.2018 sequence gains no new
  publication and no new confirmation vote. If an already-existing false
  confirmed inward publication nevertheless satisfies the exact certificate,
  it can begin on its first proven buffered frame instead of confirmation (a
  few frames earlier). This bounded tradeoff must not recreate the reverted
  eight-second dwell or otherwise change transition acceptance.
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
- Preview scene correlation is safest as a rejection/diagnostic input when the
  exact pixel certificate is absent, never as positive crop authority. It needs
  captured cut/flash traces before changing policy.
- Shader or NLS-layout prewarming could reduce unrelated cold-path transition
  cost without changing pixels; it should be evaluated separately with timing
  telemetry.
- Queue growth, detector-cadence reduction, scene-derived geometry, and NLS
  interpolation/crossfade are broader designs with materially larger latency or
  image-integrity risk and remain out of scope.

## Compatibility note

This story preserves VP-0082's exact identity, atomic crop/NLS, bounded queue,
and no-wait rules, and VP-0136's decision count, vote count, and publication
order. It deliberately supersedes VP-0124's blanket prohibition on backdating
inward transitions only for the exact per-frame certificate above; it does not
claim unqualified compliance with that older invariant.

## Related stories

- VP-0082: Buffered active-picture lookahead for Alpha.
- VP-0124: Safe outward active-picture lookahead.
- VP-0136: Prevent transient same-axis inward aspect switches.
- VP-0163: Prevent full-raster flashes during overlay confirmation.
