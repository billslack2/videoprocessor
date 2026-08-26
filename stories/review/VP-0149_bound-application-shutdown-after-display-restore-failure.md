# VP-0149: Bound application shutdown after display-restore failure

## Status

Review — implemented, independently reviewed, built, tested, pushed, and deployed
on 2026-08-26. Physical projector disappearance/renumbering and Alt+F4 remain the
user acceptance checks before Done.

Confirmed source baseline: `origin/v1.2.001-beta` at
`2cfbaf2d36a8a848743714178b3fc2861be2d127`. GitHub reports this branch as the
current default and latest beta integration branch.

Implementation branch: `codex/vp-0149-bounded-shutdown`.
Clean worktree:
`C:\Videoprocessor\vp\git-main\stories\.vp-0149-bounded-shutdown`.

Initial architecture review confirmed that strict unresolved-restoration gating
must remain for renderer replacement, while explicit application shutdown needs a
separate attempt-bounded terminal policy. Source work has started at the dialog retirement
state machine and renderer retirement service seams.

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
- **Application shutdown:** perform one best-effort external-state recovery attempt,
  then terminally release the renderer on the retirement worker and allow capture
  cleanup and `EndDialog` to complete. Missing hardware must not cause retirement
  retry-token livelock. This release is backend opt-in: VP Renderer may acknowledge
  safe abandonment after its local resources are retired, while DirectShow/madVR
  graph-owner cleanup remains fail-closed.

The minimal bug fix must also:

- stop creating new handoff-retry tokens after termination is requested;
- cancel or suppress fullscreen focus/retarget and display-recovery activity once
  shutdown begins;
- log shutdown retirement purpose, attempt outcome, and `restored` versus
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
2. Alt+F4 with a permanently failing or missing display target performs no more
   than one shutdown restoration attempt, does not enter retry-token livelock,
   and logs `external-state-unverified` with the failure reason. Calls already
   inside Windows, driver, or renderer thread joins remain outside a wall-clock
   deadline in this story.
3. No additional renderer-retirement retry tokens are queued after application
   termination is requested.
4. A normal renderer-to-renderer handoff with unresolved external state remains
   blocked; the shutdown exception cannot admit a successor. A backend that does
   not explicitly acknowledge safe terminal release also remains retained during
   shutdown.
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

- Always-failing retirement during shutdown: one restoration attempt, no
  unbounded tokens, worker-owned renderer release.
- Failed ordinary handoff: successor remains blocked.
- Failed non-abandonable DirectShow-style backend: shutdown retains ownership and
  does not apply VP Renderer's display-state exception.
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
- The correction has an attempt-bounded scope and a test seam in
  `RendererRetirementService`/dialog state reconciliation.
- Windows display-topology failure remains non-destructive: the shutdown path will
  not guess a replacement target.
- Validation can independently prove both required branches: strict replacement
  blocking and shutdown completion without retirement retry-token livelock.
- Implementation will use a clean worktree based on the confirmed remote beta tip.

## Implementation evidence

- Source commit: `029edb7f751d1d7182db8794baf4d9c53f2d5d58` on
  `codex/vp-0149-bounded-shutdown`.
- The commit is integrated directly above the existing VP-0148 commits
  `01502c3`, `12be56e`, and `19cf3b2`; all four changes share the confirmed
  `origin/v1.2.001-beta` base `2cfbaf2`.
- Renderer retirement now has explicit replacement-handoff and
  application-shutdown purposes. Replacement remains fail-closed; shutdown
  performs one final attempt for VP Renderer's safely abandonable display state,
  releases renderer ownership on the retirement worker only after backend
  acknowledgement, and reports verified versus unverified external state
  separately.
- A backend that declines safe terminal release enters one stable retained state;
  the dialog logs `backend-fail-closed` and disables automatic retry instead of
  spinning in a same-token loop.
- An in-flight or UI-held failed handoff converts to shutdown with the same
  token. Shutdown suppresses new fullscreen focus, retarget, display recovery,
  profile, and timer-driven reset work while preserving lifecycle reconciliation.
- VP Renderer relinquishes refresh and NVIDIA retry ownership after an
  unverified shutdown attempt, preventing its destructor from repeating the
  restoration. It acknowledges safe release only after local swapchain/GPU
  resources are already retired. The production plugin proxy forwards this
  finalization.
- The cross-DLL plugin API version is `14`, so an old renderer DLL is rejected
  instead of being called through the changed virtual interface. The EXE and DLL
  must be deployed as one atomic pair.

## Validation evidence

- Clean x64 Release rebuild succeeded for the native test DLL, GUI executable,
  and VP Renderer DLL.
- Integrated native suite: `930/930` passed in `32.6200` seconds.
- New automated coverage includes successful shutdown retirement, unverified
  shutdown worker release, failed handoff blocking, same-token conversion, and
  the plugin proxy's explicit shutdown-finalization override.
- Three independent architecture/code/behavior reviews found and closed two
  deployment blockers: missing production proxy forwarding and an unchanged
  plugin ABI version. The remaining hardware acceptance is an Alt+F4 trace with
  the physical projector disappearing or being renumbered.
- Scope note: this fix bounds application retry attempts and eliminates the
  observed token livelock. It does not impose a wall-clock deadline on a Windows
  or driver API call that is already blocked.

## Deployment evidence

- Source branch pushed to `origin/codex/vp-0149-bounded-shutdown` at
  `029edb7f751d1d7182db8794baf4d9c53f2d5d58`.
- Deployed the clean x64 Release pair to `C:\Videoprocessor\vp` with VP stopped.
- `VideoProcessor.exe` SHA-256:
  `59F008C28551D1569AAB7C0108045CF91CF3B2DA68E2F94B4ACA96E8D3D5CF70`.
- `vprenderer\VideoProcessorVPRenderer.dll` SHA-256:
  `B1D2BF90326886BE6EF01BB51BC24780E381039E3C6DC381013F83BEFCFEB730`.
- Rollback backup:
  `C:\Videoprocessor\vp\backups\VP-0149-deploy-20260826-032323-029edb7`.
- No configuration file was changed. The separate configuration editor was left
  running; the main VP process was not started automatically after deployment.
