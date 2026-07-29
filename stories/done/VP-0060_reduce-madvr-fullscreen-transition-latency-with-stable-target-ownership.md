# VP-0060: Reduce madVR fullscreen transition latency with stable target ownership

## Status

Done 2026-07-29. User validation found the fast path materially faster while
retaining stable fullscreen/windowed behavior. The completed implementation
was merged into `v1.1.015-beta` as `00585a5`.

The reviewed fast path began as `7565481`; the final merge also includes
`00585a5`, which coalesces superseded capture state before madVR startup. It
retains the existing madVR graph and performs a
covered graph-owner transaction: Stop, close/drain ingress, detach and verify
the old `IVideoWindow` owner, attach and verify the new HWND, reset the live
source epoch, Run, wait for five current-epoch downstream deliveries, cross a
composition boundary, and reveal. Any unsupported or failed operation remains
covered and falls back to the terminal full-rebuild path.

Threading, DirectShow, and MPC/mpv-style player reviewers approved the final
lifecycle ordering. Their rapid-reversal, target-lifetime, mode-change, stale
generation, and rollback-failure blockers were resolved before deployment.
The clean x64 Release build completed with zero warnings/errors and all
265 tests passed. The deployed executable and VP Renderer plugin hashes match
the exact build; `VideoProcessor.cfg` was preserved byte-for-byte.

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

Automated validation added:

- retarget target transport and Stop-before-ingress-drain barrier;
- Graph/GraphRetarget coalescing in both arrival orders;
- latest pre-selection retarget target selection;
- retarget failure retaining black/restart requirement and closed ingress.

Final deployed-session validation showed a clean madVR graph build in 1031 ms
(906 ms renderer creation, 125 ms connection) and first live-frame reveal in
2063 ms. No reset, renderer, graph-build, restart, or crash failure was
logged. A stale capture-state callback arriving after capture stop was rejected
before ingress admission, as designed. Alpha later initialized and revealed in
813 ms without error.

## Acceptance criteria

- No stale-frame flash, UI hang, reset/retirement failure, or target lifetime
  violation was observed in the completed validation.
- The fast path retains explicit capability gating and automatic full-rebuild
  fallback.
- Normal madVR fullscreen/windowed transition latency improved materially while
  retaining current safety evidence.
