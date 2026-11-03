# Paused source measurement coherence repair

## Reproduction and evidence

The deployed `aee2c7dfe0ff47aa9372b79c6afc200440f36e34` runtime failed to
measure a paused scope source reliably. The preserved 64-frame native capture is
byte-identical across every frame (SHA-256
`b9461e4459f1e06ba34cb9f533d044d11695bec65af49fc84d8829d7cd9e3a46`).
Independent native boundary inspection establishes `[0,263,3840,1897]`, a picture
ratio of `3840/1634 = 2.350061`. The input raster remains 3840x2160.

The capture interval contains 710 failed legacy physical observations: 682 spatial
rejections and 28 deadline expirations. The legacy observer also rejects a single
unbudgeted replay, so increasing its timeout does not repair its spatial rule.
The broader log alternates successful physical measurements and Unknown while
retained/applied geometry remains stable. Matching pixels for those earlier
successful log entries were not captured; their exact failure sequence is not
claimed reproduced. Source ordering and completion races were separately found
by code review and deterministic tests.

This worktree began at freshly fetched beta `a591ad3b532771911e85f8ef026958279f2065f0`
and retained the cumulative deployed `aee2c7df` history. It does not replace that
history with an arbitrary existing local branch.

## Design changes

- The common spatial worker is the production geometry producer, including the
  converted-P010 analyzer. Foreground caches and OSD consumption cannot invoke a
  competing legacy geometry observer. Legacy inspection/reference APIs remain.
- Owned, byte-identical source storage may reuse completed geometry facts under
  compatible source metadata. Current crop/caption verification is rerun; votes,
  masks and leases are not reused. Mutable DirectShow samples are never cached.
- Raw publication is monotonic. Missing work can finish, but late timeouts and
  alternate nominees cannot overwrite completed current geometry. Derived
  retention keys include the immutable raw-result revision.
- Geometry, work completion and crop permission are distinct. A negative completed
  result is not retried forever; unfinished jobs remain eligible only under the
  existing dispatch permissions. Ready evidence does not authorize fresh work.
- Current-caption verification can complete the same spatial nominee without an
  extra confirmation vote. A bounded latest-source checkpoint replaces one prior
  provisional evaluation; older proof cannot rewind newer temporal state.
- Retention handoff rechecks the adopted aperture using the same source facts.
  Spatial subtitle inspection validates its exact current certificate instead of
  demanding the legacy detector's fixed padding.
- Native perpendicular edge support now participates in global reconciliation.
  It prevents a confidently wrong inset when the coarse topology misses a broad
  dim strip. Contradiction without a proven alternative yields partial geometry,
  never a forced standard aspect. Candidate dominance and its limits are recorded
  in [the design](SOURCE_PICTURE_REDESIGN.md).

## Validation before runtime qualification

All 1,002 mandatory native contracts pass in
`artifacts/qualification/runs/20261101-230857-80e95dff`. This includes actual-pixel
caption completion, both raw publication race orders, wrong identities and storage,
negative/completed/unfinished distinctions, generation resets, crop preservation,
mutable converted samples, and scope transitions with pixel noise.

The current paused capture passes 64/64 bounded-worker measurements at the exact
native bounds; current-caption crop verification passes on all 64. Completed
geometry is reused on the following 63 identical frames. A post-reconciliation
replay peaks at 6.832 ms for geometry/native verification/darkness; caption work is
separate. These replay timings are not live latency guarantees.

The seven saved corpus sets pass 479 frame-instance regression checks. First-frame
native row profiles independently verify the scope boundaries; this is not 479
independently labelled source images. The older paused56 set is `[0,275,3840,1884]`,
not the current scene's bounds. The initial incorrect-hypothesis failure and its
corrected replay are both retained. Grid/canvas, menu/full raster and sun/full
raster controls remain distinct outcomes. The prior bright native capture passes
57+93 frames at `[0,7,3840,2160]`, with zero mismatches and a bounded peak of 5.7553 ms.

Private pixels and local replay logs remain outside the repository under
`E:/codex/videoprocessor/evidence`. No source frames are committed.

Actual standard/beta renderer and motion-suite qualification must be recorded for
the final pinned binaries separately. CPU and saved-frame success alone does not
establish live transition, caption rendering or deployment acceptance.

## Runtime scheduling follow-up

The first clean-commit full run `20261101-231143-220c418e` built the matching
Release runtime but failed four of 1,017 standard tests. Production-mode fixture
expectations and both-bar UI scan coverage were corrected while retaining pixel
preservation and 24-source observation checks. Follow-up `20261101-233224-86c3c7b5`
passed seven of nine checks; only the no-lookahead Classic pose failed in each
renderer. Black and reconstruction regressions pass on the new producer.

Dispatch diagnostics in `20261101-234143-c37b0ac7` show both jobs admitted while
sources are owned, but exact results usually arrive after presentation. The first
bounded preparation attempt (`20261101-235116-3c9ba23a`) passed both new native
contracts but failed both actual renderer tests: timer polling consumed about
31 ms against a 16.683 ms input interval and allowed overflow to retire the head.
The repair therefore uses completion notification with a lifetime-owned signal,
serialized native-to-subtitle dependencies and one unchanged enqueue deadline.
These failed runs remain evidence; final matching-runtime qualification is still
required. Original translated-pose and 24 distinct presented-source assertions
remain mandatory.

Completion-notification focused run `20261101-235740-0cdcc74d` passes seven of
seven checks: five preparation/publication contracts and both actual zero-lookahead
Classic pose cases. Sampled successful stages finish in 7.5-8.2 ms against the
16.683 ms interval; these are observed values, not general latency guarantees.
The standard renderer preserves the committed pose across 124 distinct observed
sources, beta across 123, both with natural scheduled and skipped analyses. The
original pose/coverage assertions are unchanged. Final shared raw-publication
notification coverage and full qualification follow on a clean source pin.

The clean `8b49f560` full run `20261102-000112-e441a669` passes all 1,023 standard
checks (1,008 native plus 15 renderer tests). Beta passes 13/15: the decoded-DV
unknown-input-repeat and Black persistent-caption cases fail at 60 Hz after cue
and continuity changes. The untranslated raw caption is clipped by the retained
native presentation rectangle. This is a real safety failure, not changed physical
picture geometry. The final follow-up restores independent future glyph prefetch
for queued input while retaining serialized zero-lookahead preparation, and makes
advanced-mode crop permission conditional on exact current composition, confirmed
absence, or the explicit initial shader-building exception. Missing work otherwise
requires source visibility protection, without modifying measured native bounds.
Full qualification remains unsatisfied until that follow-up passes both variants.

The visibility-policy native run `20261102-002311-257d1252` passes 1,009/1,010;
the remaining initialization fixture omitted the renderer's post-resolution
admission bridge. Its corrected isolated check passes in
`20261102-003344-c24da149`, retaining all wrong-context and same-source replay
rejections. Older tests that equated native geometry with every presentation
aperture now assert immutable native bounds, current raw visibility and immediate
return to native presentation on fresh qualified composition. Optional fill is
checked in both directions; contradictory current raw visibility defeats claimed
caption absence. No sticky wider-viewport state was added.

Focused run `20261102-002828-53d54dda` passes both Black persistent-caption cases
and both zero-lookahead Classic cases. The beta one-input-repeat test repeated
correctly, then sources 81-82 were discarded before source 83; its required adjacent
successor therefore failed. That failure is retained. Diagnostic follow-up
`20261102-003910-76a1b039` passes the unchanged repeat and exact-adjacent-successor
assertions at both 59.94 and 60 Hz. Queue policy, overflow, backlog and due-source
retirement logs are now preserved, alongside pressure-neighbor original-source
metadata. No scheduling rule, input cadence, queue setting or fault-injection
eligibility was changed to obtain the pass. Full clean-pin qualification remains
required; this focused result alone is not deployment acceptance.
