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
