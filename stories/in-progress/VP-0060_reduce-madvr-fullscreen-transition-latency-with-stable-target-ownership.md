# VP-0060: Reduce madVR fullscreen transition latency with stable target ownership

## Status

In Progress. User A/B comparison against an older build confirms the merged
VP-0041 path remains at least one second slower for madVR fullscreen/windowed
transitions. Implementation is on `codex/vp-0060-fast-fullscreen`, based on
the merged default branch `v1.1.015-beta`, in worktree
`C:\Users\bslac\vp\worktrees\vp-0060-fast-fullscreen`.

The first phase will add monotonic phase telemetry, reject obsolete target
revisions before graph construction, and then implement a fullscreen-only
fast path that retains black cover, Stop/drain/reset/Run, current-epoch
preroll, and full-rebuild fallback.

## User story

As a VideoProcessor user, I want fullscreen/windowed madVR transitions to
complete closer to Alpha transition speed without exposing an old frame,
hanging the UI, or weakening graph and COM ownership rules.

## Evidence and problem statement

The final VP-0041 deployed session completed all 22 resets and all 18
asynchronous DirectShow retirements without a lifecycle failure. Ordinary
Alpha transitions took 781-875 ms. Normal madVR transitions took
1204-2250 ms.

The fixed three-second policy hold is gone. First-current-frame evidence now
advances the post-start reset within roughly 93-203 ms of its request. Manual
madVR graph resets reveal in roughly 172-250 ms, so removing the post-start
purge has limited benefit and would discard proven stale-state protection.

Most remaining time comes from stopping and retiring the old graph, creating
and connecting a new madVR graph for a different target HWND, and reaching
initial downstream preroll. A rapid fullscreen reversal also produced a
3984 ms aggregate cover interval because one target generation was built and
stopped before it could reveal.

## Required investigation

1. Add millisecond phase telemetry for:
   - target intent and revision publication;
   - old graph stop, drain, and terminal retirement;
   - graph construction, filter connection, ready, and run;
   - first pre-reset current frame;
   - post-start reset stop, settle, source reset, run, and preroll;
   - final composition boundary and reveal.
2. Remove side effects from post-start diagnostic target lookup. Log the
   snapshotted bound target HWND, desired target HWND, and both revisions.
3. Test a short keyed fullscreen-intent coalescing window or revision check
   before graph Build/Run so a superseded target never receives an expensive
   graph construction.
4. Prototype one of these larger designs:
   - preferred: one persistent presentation host/surface whose outer window
     placement and style change between embedded and borderless fullscreen;
   - alternative: an explicit asynchronous DirectShow graph-owner
     `RebindTarget` transaction with capability detection and fallback to a
     full rebuild.

## Safety constraints

- Do not skip the madVR post-start Stop/reset/Run purge merely to recover
  200-300 ms.
- Do not synchronously wait on graph teardown, retargeting, or apartment
  shutdown from the UI thread.
- Preserve the VP-0041 black shield until current-epoch downstream preroll and
  the composition boundary complete.
- Keep the old target HWND alive until the graph owner acknowledges detach or
  retarget completion.
- Any in-place path must fail closed and fall back to the proven full rebuild.
- Preserve explicit two-second display-mode hardware settling.

## Validation

- Hundreds of rapid fullscreen/windowed reversals.
- YouTube TV channel changes and exits during every retarget phase.
- Close, reset, madVR-to-Alpha, and Alpha-to-madVR during retarget.
- Multi-monitor, DPI, focus/input, DWM, DirectFlip/MPO, and display-change
  behavior.
- Assertions that stale generations cannot reveal and old target HWNDs remain
  valid until graph-owner acknowledgement.
- A/B timing distribution showing a reliable improvement, not only a lower
  best case.

## Acceptance criteria

- No stale-frame flash, UI hang, reset/retirement failure, or target lifetime
  violation in the soak matrix.
- The fast path has explicit capability gating and automatic full-rebuild
  fallback.
- Normal madVR fullscreen/windowed transition latency improves materially
  across median and tail percentiles while retaining current safety evidence.
