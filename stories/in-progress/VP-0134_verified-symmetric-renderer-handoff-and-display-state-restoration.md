# VP-0134: Verified symmetric renderer handoff and display-state restoration

## Status

In Progress (2026-08-18). The developer confirmed `origin/v1.2.001-beta` as
the implementation base. The first safe-handoff implementation slice is
committed as `7a1da9d9` and pushed to
`origin/codex/vp-0134-renderer-handoff` from the clean worktree
`C:\Users\bslac\vp\worktrees\vp-0134-renderer-handoff`. The branch is based
on `bb8f5c1c84ff48e9258ebecb03502d259ab94aa9`.

Readiness review: renderer selection, callback, graph, swapchain, refresh-rate,
NVAPI, window, and command lifetimes are mapped in this record; the required
clean-start boundary and failure policy are explicit; deterministic seams and
live comparison criteria are defined; and implementation is isolated from the
dirty authoritative source checkout. NVIDIA automatic-policy restoration
(`RESET` versus replay) and physical wire behavior still require supported-
driver/projector validation before final acceptance, but do not prevent the
coordinator, generation safety, retryable restoration, and automated failure
paths from being implemented and reviewed now.

The source branch remains in progress rather than Review because the first
slice establishes the safe renderer-generation and Alpha-owned restoration
boundary, while the renderer-neutral snapshot/coordinator, asynchronous
stabilization, NVIDIA policy qualification/readback, broader deterministic
failure seams, and physical repeated-switch matrix below remain open.

## Implementation progress (2026-08-18)

Implemented and pushed in `7a1da9d9`:

- Renderer callbacks and DirectShow notification-window messages now carry a
  renderer generation. State, detail, restart, notification, and actual graph-
  event messages are rejected when stale or when no matching renderer exists.
- DirectShow notification instance data carries the generation, notification
  is revoked during graph teardown, and the dialog now acts on event codes
  drained from `IMediaEventEx::GetEvent` instead of treating notification
  `wParam` as an event code.
- The existing capture-ingress close/drain and asynchronous DirectShow
  retirement boundary is preserved. Alpha retirement now fails closed when
  required external state cannot be restored, retains the outgoing object for
  one-second retry, and blocks construction of the successor.
- Alpha destroys its renderer, swapchain, textures, D3D device, cache, and
  related output resources before restoring the original display refresh.
  Refresh restoration requires two consecutive exact rational observations
  within two seconds and retains dirty state after failure.
- NVIDIA AVI restoration is ordered after the final refresh modeset,
  re-resolves the saved display name to a current NvAPI display ID, and retains
  baseline/ownership state on lookup or `SET` failure so retirement can retry.
- The optional Alpha renderer ABI was advanced from 12 to 13 so generation
  identity is also enforced across the plugin boundary.

Automated evidence for this commit:

- Serialized x64 Release solution build: passed. Existing compiler/Qt
  deployment warnings remain; no new build error was introduced.
- Native MSTest suite: 859 passed, 0 failed, 0 skipped.
- Standalone configuration/UI integration suite: passed (exit code 0).
- New focused generation-gate and exact-refresh verification coverage: 15
  passed, including equivalent fractional rates, near-rate rejection,
  consecutive stable observations, query-failure reset, and stale generation
  rejection.
- Deployment (2026-08-18): rebuilt the clean committed branch so the binary
  identifies `7a1da9d9`, generated the verified 57-file release allowlist, and
  deployed it to `C:\Videoprocessor\vp`. All 57 replaced files were backed up
  at
  `C:\Videoprocessor\vp\backups\deploy-20260818-215320-pre-vp0134`.
  `VideoProcessor.cfg` remained byte-identical with SHA-256
  `34F339C6CEAB9426BF9461F99389A1A70097B4130BFEE1346D9C9B3A337FE7F9`,
  and the mutable Alpha shader cache was also preserved. This is deployment
  evidence only; it does not replace the live renderer-switch matrix.

Still required before Review/Done:

- Move the restoration journal and renderer-neutral pre-construction snapshot
  into one application/dialog-owned coordinator covering both directions.
- Make display stabilization asynchronous so failed/mismatched observations
  never sleep on the UI thread.
- Compare complete target/path identity and observable Advanced Color state,
  detect target replacement/ownership conflict, and prevent stale-baseline
  writes after hotplug or topology changes.
- Qualify Nvidia automatic/default/override restoration on supported drivers,
  add post-restore readback/conflict handling, and preserve existing override
  semantics.
- Complete deterministic fault seams for retry, topology, DirectShow partial
  teardown/OSD failure, and then run the repeated Alpha/madVR hardware matrix.

## User story

As a VideoProcessor user switching repeatedly between madVR and the Alpha/VP
Renderer, I want each incoming renderer to start with the same observable
renderer, display, color-signaling, timing, and queue conditions as a clean
application start, so neither renderer inherits stale state or delayed work
from the renderer it replaces.

## Problem statement

The GUI currently stops the active renderer, receives `RENDERSTATE_STOPPED`,
destroys that renderer, and immediately lets `UpdateState()` construct the
selected replacement. This gives the C++ objects a mostly ordered lifetime,
but there is no verified handoff barrier between destruction and construction.

That distinction matters because the outgoing renderer owns more than one
kind of state:

| State class | Examples | Required boundary |
| --- | --- | --- |
| Renderer/swapchain-local | DirectShow graph and filters, madVR COM instance, D3D device/context, Alpha swapchain, `SetColorSpace1`, back buffers, queues, presentation counters | Stop all producers, destroy the complete generation, and reject late callbacks; do not copy or reset these settings on the next renderer |
| Window/process-local | Child-window ownership, fullscreen/exclusive state, cursor/window styles, DirectShow notification target, renderer timers and runtime commands | Return the VP host to a known state and retire all work carrying the old generation |
| Display/driver-global | Active DisplayConfig timing, incidental path changes, Windows Advanced Color/HDR observations, NVIDIA AVI InfoFrame, driver output state, external refresh-command effects | Restore only VP-owned fields to a renderer-neutral baseline, re-query and verify them, then permit incoming construction |

`IDXGISwapChain3::SetColorSpace1` is swapchain-local and is removed by
destroying Alpha's swapchain. It must not be "reset on madVR." API acceptance
also does not prove the HDMI/DisplayPort wire signal. The dangerous state is
the display/driver-global state and any old-generation work that can run after
the replacement starts.

The clean-start claim must be bounded to fields VP can own or observe. VP
cannot prove every private madVR or driver-internal value through public APIs.
Diagnostics must say `driver readback` or `wire state unverified` rather than
claiming physical verification without sink evidence or a protocol analyzer.

## Confirmed source findings

### Renderer state-machine and DirectShow findings

1. `CVideoProcessorDlg::RenderStop()` disables new frame delivery and calls
   the renderer's `Stop()`. The posted stopped-state handler calls
   `RenderRemove()`, deletes the renderer, and then immediately calls
   `UpdateState()`. No display stabilization state exists between those steps.
2. DirectShow teardown releases graph notifications, detaches the video
   window, disconnects the live source, releases graph interfaces and clock,
   destroys the source and renderer filter, and frees the media type. A newly
   selected madVR renderer therefore gets a new graph and COM instance; it
   does not need an additional `DirectShowVideoRenderer::Reset()` merely
   because it followed Alpha.
3. The generic HDR renderer clears VP's native madVR OSD before base-class
   graph destruction. VP-managed shader chains belong to the outgoing madVR
   COM instance and are reapplied to a new graph. Persisted user rule/profile
   selection is intentional policy state, not leaked GPU state.
4. Renderer state callbacks and DirectShow window notifications do not carry a
   renderer-generation identity. A queued old-generation notification can be
   interpreted against the current dialog state unless the handoff explicitly
   invalidates or tags it.
5. Setting the atomic frame-delivery flag to false prevents a new capture
   callback from entering the renderer path, but does not by itself prove that
   a callback which already observed true has exited `OnVideoFrame()` before
   Stop/destruction. The transition needs an explicit in-flight drain or an
   equivalent lifetime guard.
6. Queue and graph re-prime behavior is already owned by VP-0043, VP-0061,
   VP-0063, and VP-0078. This story may invoke their accepted generation-safe
   operation after the switch barrier, but must not invent a second reset
   policy or use queue depth alone as evidence of display-state correctness.
7. DirectShow registers `IMediaEventEx::SetNotifyWindow` without instance
   data. The dialog also inspects notification-window parameters as though
   they were DirectShow event codes, even though the renderer obtains the real
   codes by draining `IMediaEventEx::GetEvent`. Already-posted notifications
   therefore lack trustworthy graph identity and their meaning can be logged
   or acted upon incorrectly.
8. Renderer stop cancels EOTF/LLDV timers but does not comprehensively retire
   the pending queue-reset timer and its graph/reset reason. Old-generation
   reset work can therefore remain eligible while the backend is replaced.
9. Base DirectShow destruction calls graph teardown paths that can throw. A
   failed partial build/disconnect or stop timeout must not escape a destructor
   or UI transition and terminate the process; teardown must be best-effort,
   idempotent, non-throwing, and report every incomplete release.
10. If native madVR OSD removal fails, the current path can delete the submitted
    `HBITMAP` while madVR may still reference it. OSD resource retirement must
    remain safe until the service/renderer is definitively released.

### NVIDIA driver/output-signaling findings

1. Alpha's `NvidiaBt2020Reporter` resolves the active NVIDIA display, captures
   the current AVI InfoFrame, writes extended BT.2020 colorimetry, and reads
   selected fields back. This is display/output-global state.
2. `Restore()` makes one NVAPI `SET` attempt, then clears its active/display
   tracking and unloads NVAPI regardless of the restore result. A failed or
   ineffective restore is therefore forgotten and cannot be retried.
3. Alpha currently restores the AVI InfoFrame in `Impl::~Impl()` before
   destroying the swapchain and D3D/libplacebo objects. Its scoped refresh-rate
   object is destroyed later, so the final display-global mutation can be a
   modeset after the InfoFrame restoration. Swapchain destruction or that
   modeset may make the driver regenerate its output signal; the final state is
   not re-read.
4. The current code does not use NVIDIA persistent InfoFrame overrides or the
   NVIDIA HDR control API. This story must not introduce a persistent override
   or disturb a user-created override.
5. NVAPI readback is driver-state evidence, not proof of packets received by
   the sink. Display IDs and automatic driver policy may also change across a
   modeset, hotplug, topology transition, or driver reset, so restoration must
   re-resolve the target after display stabilization.
6. Replaying a concrete `GET` payload may not be semantically identical to
   restoring NVIDIA's automatic policy. Implementation must review and test
   `GET_DEFAULT`, `GET_OVERRIDE`, `GET_PROPERTY`, and `RESET` semantics before
   choosing replay versus reset. It must never create or erase a persistent
   user override as an incidental handoff action.

### Windows display-stack findings

1. Alpha changes active-path refresh timing with `SetDisplayConfig` and uses
   `ChangeDisplaySettingsExW` as a fallback. That state survives swapchain
   destruction.
2. The refresh scope marks itself changed only after post-change validation.
   A successful `SetDisplayConfig` followed by an inactive target and a failed
   fallback can return while restoration is still marked unnecessary. The
   fallback-success/verification-failure path has the same risk.
3. Refresh restoration clears its changed flag even when restoration fails,
   and a successful API return is not followed by exact rational verification.
   Integer `DEVMODE.dmDisplayFrequency` is not sufficient proof that an
   original fractional rate such as 59.940/59.941 was restored.
4. `SDC_ALLOW_CHANGES` permits Windows to choose a compatible configuration.
   VP must re-query all relevant path fields instead of assuming only refresh
   changed.
5. `WM_DISPLAYCHANGE` is a useful wake-up/invalidation notification, not proof
   that target timing, Advanced Color, driver signaling, DWM, or the sink has
   stabilized.
6. Refresh-rate commands are detached `cmd.exe` processes delayed with
   `ping.exe`; VP closes their handles immediately. VP-0135 separately removes
   the artificial delay and launches configured commands directly. These are
   intentional user-configured external actions, so this story must observe
   their launch boundary without treating arbitrary script effects as
   renderer-owned reversible state.
7. VP does not currently toggle Windows Advanced Color/HDR. Because madVR may
   change observable display state, the handoff must inventory and compare it,
   but VP must not toggle it unless VP recorded ownership of that change.

## Required design

### Renderer-neutral coordinator

Introduce one application/dialog-owned `DisplayStateCoordinator` (name not
prescriptive) above both renderer implementations. It owns a monotonically
increasing transition ID, target identity, renderer-neutral baseline,
VP-owned mutation journal, old-generation command records, and retryable dirty
state. Renderer destructors remain defensive cleanup, but they are not the
sole transaction owner.

Identify a physical target with stable DisplayConfig data: adapter LUID,
source ID, target ID, GDI source name, and monitor/device identity where
available. Do not key restoration only by `HMONITOR` or `\\.\DISPLAYn`.

Capture the first neutral baseline before the first renderer can mutate the
display. Do not let Alpha snapshot a state that madVR may already own. Do not
reapply a frozen startup snapshot wholesale: restore only fields VP journaled
as owned, and rebase the neutral observation after a genuine, stable external
user/topology change while no renderer owns the display. Never write a stale
snapshot onto an inferred replacement target.

### Symmetric switch barrier

Every Alpha -> madVR, madVR -> Alpha, repeated Alpha -> madVR -> Alpha,
same-renderer rebuild, and application shutdown must use the same ordered
handoff transaction:

1. Allocate a transition ID and invalidate the outgoing renderer/command
   generation.
2. Stop new capture-frame admission and wait until all callbacks that could
   have entered the outgoing renderer have drained.
3. Stop renderer-owned worker/delivery threads and the DirectShow graph as
   applicable.
4. Disable and drain old DirectShow event notification; reject queued renderer
   state/window messages whose generation is no longer current.
5. Completely destroy the outgoing graph, filters, COM interfaces, swapchain,
   D3D resources, child windows, OSD resources, and fullscreen/exclusive
   ownership.
6. Cancel scheduled-but-not-started renderer-owned actions. Do not cancel or
   terminate an external refresh command already launched under the VP-0135
   user-command contract.
7. Re-query topology and resolve the intended physical target again.
8. Restore VP-owned display mode/path state. Any successful mutating API call
   must mark the target dirty immediately; dirty remains set until exact
   re-query proves restoration.
9. After the final modeset and target stabilization, re-resolve the NVIDIA
   display and restore the appropriate saved/automatic AVI policy. Verify by
   driver readback; keep failed restoration pending and retryable.
10. Observe Advanced Color/HDR, output target, window ownership, and other
    clean-start fields. Restore only fields for which VP has an ownership
    record.
11. Require a bounded stable observation window, for example two or three
    matching samples 50-100 ms apart within a configurable/internal 2-5 second
    deadline. `WM_DISPLAYCHANGE` wakes the check but cannot complete it alone.
12. Reset display-rate measurement and generation-local queue/presentation
    baselines. Apply the existing destination-specific re-prime policy only
    after the new renderer becomes ready.
13. Construct the incoming renderer and let it negotiate only its own output
    contract.

The transition must be asynchronous/state-machine driven; do not block the UI
thread with sleeps or a nested unsafe message pump. The replacement renderer
must not exist concurrently with unresolved outgoing global state.

### External refresh-command boundary

VP-0135 owns removal of the `ping.exe` pre-launch timer and direct asynchronous
execution of the configured command. VP-0134 must not cancel, terminate, or
wait for an intentionally launched arbitrary user command as though it were a
renderer worker. Record the transition and command-launch ordering in
diagnostics, and state explicitly that clean-start equivalence cannot include
unobservable or irreversible side effects produced by user scripts.

### Failure policy

Fail closed by default. If a VP-owned required field cannot be restored and
verified by the deadline:

- leave frame delivery disabled and do not automatically construct the next
  renderer;
- preserve the dirty journal and original data for Retry;
- report the exact target, API result, observed mismatch, and remaining dirty
  fields;
- offer an explicit user-authorized `continue with unverified display state`
  path only if product policy accepts it; and
- if the target disappeared, wait for stable topology or request a target
  decision rather than applying state to another display.

Shutdown performs bounded best-effort restoration and logs residual dirty
state. Crash recovery is not guaranteed by RAII and may be tracked separately
if required.

## DirectShow-specific requirements

1. Give renderer callbacks, state messages, DirectShow notifications, reset
   requests, timers, and first-frame evidence an explicit renderer generation.
   Ignore and log stale generations without dereferencing the new renderer.
2. Add a capture-delivery quiescence mechanism proving that no `OnVideoFrame`
   call remains in the outgoing object before `Stop()`/delete. Preserve capture
   thread performance and existing lock order; do not solve this with an
   unbounded UI-thread wait.
3. Prove graph shutdown and window detachment complete before the display
   barrier begins. Clear the native madVR OSD while its service is valid and
   release all graph/filter/window interfaces before Alpha construction.
4. A new madVR graph is the reset boundary. Do not call in-place `Reset()` on
   an object being destroyed or on the newly built graph merely to clear
   Alpha state. Use existing startup/handoff re-prime policy only for the
   destination generation and keep it distinct from display restoration.
5. Preserve intentional runtime shader/NLS/profile selection and reapply it to
   a new madVR graph. Prove no physical shader, OSD bitmap, media type, HDR
   metadata, queue sample, clock epoch, event, or old-generation reset request
   survives the retired graph.
6. Route the actual event codes drained from `IMediaEventEx::GetEvent`; do not
   infer them from notification-window `wParam`. Put a transition/generation
   cookie in notification instance data and revoke notification before graph
   teardown.
7. Make Stop and partial-build teardown non-throwing and release-complete.
   Verify both raw and converted queues and all conversion/delivery/subtitle
   workers, not only debug assertions. A blocked `Receive` must be released by
   the serialized flush/shutdown contract rather than an arbitrary sleep.
8. Cancel all generation-owned queue/reset/EOTF/LLDV work at barrier entry.
   The existing delayed startup `Reset()` operates on the same madVR COM
   instance and is not "from scratch"; retain it only under its separately
   proven startup policy, remove its fixed sleep where safely possible, and
   never use it as the renderer-switch cleanup transaction.
9. If named OSD removal fails, keep its bitmap valid until madVR service and
   renderer release makes further use impossible. Do not trade a stale overlay
   diagnostic for a use-after-free risk.

## NVIDIA-specific requirements

1. Move final InfoFrame restoration to the process-level barrier, after Alpha
   swapchain/device destruction and after the final display modeset.
2. Resolve the NVIDIA display ID from the stable current target at restore
   time. Record driver version/branch and every NVAPI status, including unload.
3. Preserve restore-pending data on any failed `SET`, readback mismatch,
   hotplug, or unavailable target; permit bounded retry.
4. Detect an unexpected current InfoFrame differing from both the Alpha-owned
   value and baseline as an ownership conflict. Do not blindly overwrite a
   newer third-party/user owner.
5. Never use a persistent `SET_OVERRIDE`, and do not clear an existing user
   override. Determine through hardware-backed review whether driver-auto
   policy requires NVAPI `RESET` rather than replaying one concrete `GET`.
6. Keep pixel target, DXGI transport, and physical signaling separate in
   diagnostics and validation. Correct metadata cannot compensate for an
   incorrect pixel transform/range, and correct pixels do not prove signaling.

## Diagnostics

Emit one concise lifecycle record per transition, not per frame. It must
correlate:

- transition ID; outgoing/incoming renderer name, backend, and generation;
- capture-admission close and in-flight callback drain;
- graph/thread stop, destruction, stale notification count, and window/
  fullscreen release;
- target adapter description/vendor/device/LUID, source/target IDs, GDI name,
  monitor identity, and topology generation;
- baseline/requested/observed/restored resolution, rational refresh, scanline,
  rotation, scaling, and topology fields;
- Advanced Color supported/enabled state and SDR white level where available;
- Alpha swapchain contract, explicitly labeled swapchain-local;
- NVIDIA driver/display ID; current/default/override/property information when
  supported; before/applied/pre-restore/post-restore AVI fields; all NVAPI
  results; and `driver readback` versus `wire state unverified`;
- old-generation command schedule, PID/job, cancellation/completion/exit code;
- stabilization samples, `WM_DISPLAYCHANGE` hints, elapsed time, timeout or
  success; and
- destination queue/clock re-prime reason and generation, without treating it
  as evidence that global display restoration succeeded.

## Automated test requirements

Add deterministic seams/fakes for:

1. Transition generation and rejection of stale renderer-state, DirectShow
   event, timer, first-frame, reset, and display messages.
2. Capture callback entering just before admission closes; teardown must wait
   for its exit and must never call a destroyed renderer.
3. Display mutation returning success followed by target mismatch, query
   failure, fallback failure, fallback success with failed verification, and
   exact successful restoration.
4. Preservation of dirty state across restore failure and bounded retry.
5. Fractional refresh verification and complete path-field comparison after
   `SDC_ALLOW_CHANGES`.
6. NVIDIA set/readback/restore failure, ownership conflict, target remapping,
   auto/default policy, existing override preservation, and retry.
7. Direct external-command launch ordering, switch while a user command is
   running, launch failure, child-process limitation, and a
   rate-already-correct command. VP-0135 separately proves removal of the
   artificial delay.
8. Stable-observation coalescing, missing/duplicate `WM_DISPLAYCHANGE`, stale
   DXGI factory/output snapshots, hotplug, and target identity change.
9. Cold versus switched pre-construction snapshots for both renderer backends.
10. DirectShow partial-build failure, Stop/GetState timeout, disconnect failure,
    blocked delivery release, non-throwing destructor cleanup, native OSD
    removal failure, and complete COM/GDI/thread/handle retirement.

## Live validation matrix

| Scenario | Required evidence/result |
| --- | --- |
| Cold Alpha and cold madVR | Record the renderer-neutral pre-construction snapshot used as comparison truth |
| Alpha Rec.709 -> madVR SDR -> Alpha Rec.709 | Every direction passes the barrier; no stale mode, signal, command, event, frame, OSD, or queue generation |
| Alpha BT.2020 reporting -> madVR SDR/HDR | Final modeset precedes verified NVIDIA baseline/auto restoration; madVR starts only afterward |
| madVR SDR/HDR/BT.2020 -> Alpha Rec.709 and BT.2020 | Alpha does not capture madVR-owned state as neutral; its negotiated output equals a clean Alpha start |
| Repeated Alpha <-> madVR | 25-100 switches without cumulative drift, stale callbacks, queue growth, delayed commands, UI stall, or residual dirty state |
| Refresh families | Already-correct, 23.976/24, 25/50, 29.97/59.94, 30/60, 100/120, interlaced/double-rate, and VRR on/off where supported |
| Presentation modes | Windowed, borderless fullscreen, and madVR exclusive fullscreen where configured |
| Color modes | Windows HDR off/on, SDR/PQ/HLG, Rec.709/BT.2020, full/studio, and 8/10-bit-capable paths |
| Display topology | Single, extended, clone, cross-monitor/GPU move, hotplug during teardown, sleep/wake, lock/unlock, and display power cycle |
| GPU/vendor paths | NVIDIA success/failure on at least two supported driver branches; AMD/Intel paths remain no-op for NVIDIA work |
| External commands | Direct launch, switch while running, launch failure, spawned child, and no matching rule; VP-0135 proves there is no artificial delay |
| Fault injection | Mode/query/fallback/restore failures, NVAPI failures, missing notifications, stale output factory, topology change, and device removal |

Ordinary validation may use Windows/NVAPI readback and sink diagnostics. Final
NVIDIA/projector qualification should include known range/color patterns and,
when available, an HDMI protocol analyzer or trustworthy sink status page.

## Acceptance criteria

- Alpha -> madVR, madVR -> Alpha, repeated round trips, same-renderer restart,
  and shutdown all use one renderer-neutral transition coordinator.
- No incoming renderer is constructed until the outgoing generation is fully
  quiescent/destroyed and every required VP-owned global field is restored and
  verified, or the switch stops with an explicit actionable failure.
- A cold start and switched start of each backend produce the same observable
  pre-construction display snapshot and the same generation-local initial
  queue, clock, window, and output negotiation behavior.
- No capture callback, DirectShow/window notification, timer, reset request,
  first-frame event, or external command from an old generation can act on the
  successor.
- `SetColorSpace1` remains swapchain-local and is neither copied to nor reset
  on madVR. Logs do not call DXGI acceptance physical wire verification.
- Every successful display mutation immediately records dirty state; dirty is
  cleared only after exact target/path re-query proves restoration. Failed
  restoration remains retryable.
- NVIDIA restoration occurs after swapchain/device destruction and the final
  modeset, is read back, preserves automatic/user override ownership, and
  remains pending on failure.
- No renderer-owned delayed action from a retired generation can execute after
  successor construction. Intentionally launched external refresh commands
  follow VP-0135 and are explicitly outside reversible renderer ownership.
- Hotplug/topology change cannot cause a stale baseline to be applied to a
  different target.
- Existing renderer-specific startup/re-prime, NLS/shader/profile persistence,
  Alpha color contract, frame pacing, and steady-state latency do not regress.
- A clean x64 Release build, focused automated tests, repeated-switch live
  matrix, and NVIDIA/projector output validation pass with no residual dirty
  state in the final transition log.

## Non-goals

- Reverse engineering or resetting undocumented private madVR state.
- Treating queue depth, one `WM_DISPLAYCHANGE`, `SetColorSpace1`, or NVAPI
  `GET` alone as proof of clean physical output.
- Changing Alpha's tone mapping, LUT, gamut mapping, NLS, or shader policy.
- Replacing the accepted destination-specific graph/queue re-prime algorithms.
- Persistently overriding NVIDIA InfoFrames or silently changing user HDR,
  topology, calibration, or display-profile choices.
- Guaranteeing recovery after process crash; track a watchdog/recovery design
  separately if required.

## Dependencies and related work

- VP-0018: Alpha content-refresh reselection after renderer switches.
- VP-0019, VP-0064, and VP-0093: SDR BT.2020 target/transport/NVIDIA
  signaling contract and teardown restoration.
- VP-0043 and VP-0063: madVR startup/handoff graph re-prime.
- VP-0061 and VP-0084: DirectShow reset/queue behavior with asymmetric madVR
  queues; this story must not silently expand those blocked scopes.
- VP-0078: Alpha refresh/host/handoff queue re-prime.
- VP-0117 and VP-0133: presentation/output truth and authoritative output
  diagnostics.
- VP-0135: direct refresh-rate command execution without the `ping.exe` delay.

## Implementation readiness gate

Before moving this story to In Progress:

1. Confirm through NVIDIA documentation and target-hardware experiments
   whether restoring automatic AVI policy requires replay, `RESET`, or another
   non-persistent transaction, and document existing override behavior.
2. Define the exact observable clean-start snapshot and the ownership rules for
   user/topology changes, Advanced Color, and arbitrary external commands.
3. Identify the callback-drain and generation-tagging design without changing
   capture-thread lock order or adding an unbounded wait.
4. Define fail-closed UI behavior, bounded stabilization/retry deadlines, and
   the explicit user override path, if any.
5. Confirm the VP integration base under the tracker branch gate and use a
   clean implementation worktree.
