# VP-0149: Bound application shutdown after display-restore failure

## Status

Backlog — confirmed P1 shutdown-livelock bug recorded on 2026-08-26. The user
authorized implementation and deployment from the latest remote beta branch.

Confirmed source baseline: `origin/v1.2.001-beta` at
`2cfbaf2d36a8a848743714178b3fc2861be2d127`. GitHub reports this branch as the
current default and latest beta integration branch.

## User story

As a VideoProcessor user closing the application while VP Renderer owns a
refresh-rate change, I want shutdown to make a bounded best-effort restoration
attempt and then terminate cleanly even if the configured projector disappears,
is renumbered, or rejects restoration, without weakening the safety barrier that
blocks an in-process renderer replacement while display state is unresolved.

## Incident and defect evidence

At 00:40:00 on 2026-08-26, Alt+F4 was accepted for VP Renderer generation 21.
Renderer stop returned immediately and the retirement worker released the
swapchain and D3D resources. The saved 23.976 Hz refresh restoration then failed
verification while the Epson source changed from `\\.\DISPLAY2` to
`\\.\DISPLAY1` and later disappeared from the active topology.

The dialog retained each failed renderer retirement and `UpdateState()` processed
the failure before its termination branch. It queued retirement tokens 21 through
186 for 3 minutes 43 seconds. Repeated Alt+F4 could only re-enter the same state
machine, so the user had to terminate the process manually.

This is a live retry livelock, not a render-thread deadlock. It originates in the
VP-0134 strict handoff boundary and is independent of the VP-0148 queue/latency
changes.

## Scope and architectural boundary

Implement an explicit retirement purpose:

- **Replacement handoff:** remain fail-closed. An unresolved display-global
  restoration continues to block successor construction.
- **Application shutdown:** perform bounded best-effort external-state recovery,
  then terminally release the renderer on the retirement worker and allow capture
  cleanup and `EndDialog` to complete. Missing hardware must not prevent process
  termination indefinitely.

The minimal bug fix must also:

- stop creating new handoff-retry tokens after termination is requested;
- cancel or suppress fullscreen focus/retarget and display-recovery activity once
  shutdown begins;
- log shutdown retirement purpose, retry/timeout outcome, and `restored` versus
  `external-state-unverified` explicitly;
- keep renderer destruction and any final blocking restoration attempt off the UI
  thread;
- attempt/log NVIDIA cleanup independently when refresh restoration is unresolved,
  rather than silently deferring it forever.

Durable physical monitor identity and a renderer-neutral persistent dirty-recovery
journal remain VP-0134 work unless a small prerequisite is required for this bounded
shutdown fix. This story must never restore through a guessed or different current
monitor when the original physical target cannot be identified.

## Acceptance criteria

1. Alt+F4 with successful display restoration exits normally and logs a clean
   shutdown-retirement outcome.
2. Alt+F4 with a permanently failing or missing display target terminates within a
   documented finite bound and logs `external-state-unverified` with the failure
   reason.
3. No additional renderer-retirement retry tokens are queued after application
   termination is requested.
4. A normal renderer-to-renderer handoff with unresolved external state remains
   blocked; the shutdown exception cannot admit a successor.
5. Renderer destruction and final restoration work remain owned by the retirement
   worker, not the UI thread.
6. Repeated Alt+F4 during retirement is idempotent and does not queue duplicate
   operations.
7. Fullscreen focus, retarget, and display-recovery timers do not recreate work once
   shutdown begins.
8. Refresh and NVIDIA restoration outcomes are logged independently and truthfully.
9. Existing renderer handoff, reset, queue, and shutdown tests pass in an x64
   Release build.

## Required tests

- Always-failing retirement during shutdown: bounded completion, no unbounded
  tokens, worker-owned renderer release.
- Failed ordinary handoff: successor remains blocked.
- Shutdown begins while a handoff retry is pending: safely converts to terminal
  shutdown retirement.
- Physical target disappears or its GDI source name changes during Alt+F4.
- `SetDisplayConfig` returns error 5 and restore verification times out.
- Repeated Alt+F4 during retirement.
- Normal successful restoration and clean shutdown.
- Independent NVIDIA restoration failure/success diagnostics.

## Dependencies

- VP-0134 owns the broader durable physical-output journal and symmetric renderer
  handoff design.
- VP-0148 is not a code dependency; its deployed queue/latency changes must remain
  intact when this fix is integrated and deployed.

## Readiness review

- The failure is reproduced by a complete timestamped production trace.
- Renderer-local retirement, display-global restoration, UI state reconciliation,
  and process-termination ownership are identified in current beta source.
- The correction has a bounded scope and a test seam in
  `RendererRetirementService`/dialog state reconciliation.
- Windows display-topology failure remains non-destructive: the shutdown path will
  not guess a replacement target.
- Validation can independently prove both required branches: strict replacement
  blocking and bounded application exit.
- Implementation will use a clean worktree based on the confirmed remote beta tip.

