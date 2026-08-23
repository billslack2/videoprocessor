# VP-0134: In-process cold-start renderer handoff with verified display-state isolation

## Status

In Progress — implementation started from the current remote beta integration
tip on 2026-08-23.

Source baseline: `origin/v1.2.001-beta` at
`0fab90191a29c0d5e9cd22dae4e72217e7289661`.
Implementation branch: `codex/vp-0134-cold-start`.

The original VP-0134 safety slice is already integrated as `580d6483a`
(**VP-0134 enforce safe renderer handoff boundary**) and `5320d9d1c`
(**VP-0134 tag DirectShow stop completion wake**). The obsolete
`codex/vp-0134-renderer-handoff` branch is not an ancestor of beta and must not
be revived or merged.

The integrated code already has a strong normal-path renderer-local boundary:
generation-tagged callbacks, ingress closure and drain, asynchronous lifecycle
retirement, successor gating, apartment-owned DirectShow graph teardown, and
VP Renderer output-resource release before display restoration. This story
does not replace that work. It closes the remaining correctness gaps and makes
the intended boundary explicit.

## User story

As a VideoProcessor user switching repeatedly between madVR and VP Renderer,
including while VideoProcessor remains fullscreen, I want every incoming
renderer to behave like a fresh in-process start: the outgoing renderer domain
is completely retired, VP-owned display mutations are verified clean, settled
Windows state is queried again, and a new renderer is attached to the same
validated fullscreen host without restarting or leaving the application.

## Cold-start contract

This is an **in-process renderer cold start**, not an application restart and
not a request to leave fullscreen.

The application keeps the selected display and fullscreen presentation shell
stable while the renderer implementation changes underneath it:

| State that persists across the handoff | Owner |
| --- | --- |
| Dialog/event HWND, fullscreen host HWND, monitor selection, placement, topmost state, transition cover | Dialog/application |
| Capture service and current source metadata, but no renderer frame leases or queued renderer frames | Application/capture control plane |
| Settings, selected renderer, transition allocator, diagnostics, retry timers | Application |
| Physical-target identity and dirty VP-owned display journal | Renderer-neutral application service |

The outgoing renderer domain must be completely recreated:

| State that must not cross generations | Required treatment |
| --- | --- |
| Renderer instance, callback/reset binding, admission token, frame leases, queues, timers, telemetry epochs | Revoke, drain, destroy, then allocate anew |
| DirectShow graph-owner thread/apartment, graph manager, filters, live source, madVR COM object, `IMediaEventEx`, `IVideoWindow`, reference clock and notification registration | Release on the graph owner and prove release before successor construction |
| VP Renderer workers, D3D11 device/context, DXGI interfaces, swapchain/backbuffers, libplacebo GPU/renderer/textures/live cache object | Stop and release before display-global restoration |
| Live NVAPI session/display ID and renderer-local display snapshots | Close; recovery authority moves to the application journal |

A disk shader cache may persist as data. No live device-dependent cache object
may cross renderer generations.

## Completion barriers

Successor construction requires two independently evidenced barriers:

1. **Renderer-local barrier:** all graph, COM, worker, queue, GPU and callback
   resources owned by the outgoing renderer are proven gone.
2. **Display-global barrier:** every dirty VP-owned journal entry is verified
   restored on the same physical target, or the transition remains safely
   blocked with actionable diagnostics.

The old renderer object must not remain the long-term display-recovery owner.
After its local domain is gone, only the small application journal and its
retry state remain.

## Remote-state audit

### Delivered on current beta

- Renderer callbacks, DirectShow notification wakes, graph events, detail
  updates and restart requests carry a renderer generation; stale work is
  rejected.
- DirectShow notification instance data carries the generation and the receiver
  drains actual events from `IMediaEventEx::GetEvent`.
- Capture ingress is closed and drained before renderer destruction.
- Renderer destruction is detached from the UI-owned shared pointer and queued
  to `RendererRetirementService`; successor construction waits for completion.
- DirectShow graph/filter construction, control and release are serialized on a
  dedicated MTA graph-owner thread.
- VP Renderer releases workers, swapchain, textures, D3D/libplacebo resources
  and live cache objects before restoring VP-owned refresh/NVIDIA state.
- Refresh verification requires consecutive exact rational observations and
  runs off the UI thread.

### Confirmed gaps

- The existing live renderer generation is also the only token capable of
  delivering terminal `STOPPED`/`FAILED`; invalidating it at transition start
  would deadlock the handoff.
- `WM_MESSAGE_DIRECTSHOW_NOTIFICATION` currently multiplexes graph-event wakes
  and internal graph-owner completion wakes, so it cannot be blindly purged.
- DirectShow retirement can latch `retired` and report success even when forced
  cleanup did not prove every graph resource released.
- A failed DirectShow stop can enter an unbounded ingress drain while a
  downstream `Receive` remains blocked.
- `DisplayTopologySession::RestorePending` applies the saved topology, verifies
  only target membership, then applies a maximum mode and clears recovery.
- VP Renderer resolves refresh ownership only through a GDI source name and
  supplies the full active configuration with `SDC_ALLOW_CHANGES`.
- There is no application-owned display journal spanning renderer families.
- NVIDIA restoration replays an observed `GET` payload as `SET` without
  qualifying automatic/default policy or verifying final restoration.
- Advanced Color/HDR state is not observed at the handoff boundary.
- The repeated fullscreen madVR -> VP Renderer -> madVR live matrix has not
  been completed.

## Required implementation

### 1. Use separate transition, admission and retirement identities

Do not invalidate the outgoing renderer generation before its terminal
completion can be consumed.

1. Allocate a transition ID.
2. Revoke the outgoing **admission/action binding** and close new capture
   admission immediately.
3. Keep a distinct **retirement token** valid for exactly one terminal
   `STOPPED` or `FAILED` completion.
4. Stop delivery and worker work, unblock downstream delivery where needed,
   and drain generation-owned frame leases.
5. Detach the renderer from the UI and retire it through
   `RendererRetirementService`.
6. Consume and close the retirement token only after durable renderer-local
   completion.
7. Reconcile the display-global journal and wait for stable Windows state.
8. Allocate the successor renderer generation and construct a new renderer on
   the same validated fullscreen host.

Late non-terminal callbacks remain rejected as soon as admission is revoked.
A retiring domain is permitted to publish only its one typed terminal result.

### 2. Separate DirectShow graph-event and owner-completion wakes

- Give `IMediaEventEx::SetNotifyWindow` a dedicated `WM_APP` graph-event
  message carrying the renderer generation in `lParam`.
- Give graph-owner lifecycle completion a separate message/token path.
- Treat the graph-event message only as a wake to drain `GetEvent` until it
  fails; never infer an event code from `wParam`.
- During teardown call `SetNotifyWindow(nullptr, 0, 0)` before releasing the
  event interface. Purge only the dedicated retiring graph-event message, and
  only before a successor uses that message ID. The generation gate remains the
  final race defense.
- Do not copy MPC-HC's message purge mechanically: VP's current message is
  multiplexed and can carry the terminal completion that unlocks retirement.

### 3. Make DirectShow retirement truthful, retryable and bounded

- Override `RetirementSucceeded()` using proven graph-resource completeness.
- Make `Retire()` idempotent and retryable until graph resources are confirmed
  released. Set the final retired latch only after successful completion.
- Preserve the established COM owner apartment for all graph/filter operations,
  including partial-build and forced-cleanup paths.
- If `Stop`/`GetState` fails or times out, perform an explicit source-pin
  flush/abort or equivalent forced-unblock phase before the final ingress drain.
- Do not destroy a renderer while any frame lease can still invoke it.
- A third-party COM/driver call that never returns leaves the application
  covered and the transition visibly blocked; never use `TerminateThread`.

### 4. Correct existing full-topology restoration

`DisplayTopologySession` is already an application-owned display authority and
must be reconciled with VP-0134 rather than left as a competing restorer.

- Remove maximum-mode selection from `RestorePending`.
- Keep maximum-mode selection only as an intentional, journaled target-only
  session mutation.
- Before clearing recovery, verify the physical target identities and the
  exact restored topology: active paths, source positions, source/target mode,
  rational refresh, scaling, rotation and relevant path flags.
- Retain the durable recovery record on any mismatch.

### 5. Move VP-owned display recovery into a renderer-neutral journal

Add an application-owned display mutation journal and integrate it with the
existing topology session. It records only fields VP actually changed, marking
each entry dirty immediately after a successful mutating call.

Each entry contains:

- transition and mutation IDs;
- adapter LUID, source ID, target ID and GDI routing name;
- `DISPLAYCONFIG_TARGET_DEVICE_NAME.monitorDevicePath` and stable
  ContainerId/EDID identity where available;
- original and requested values;
- mutation result, observed collateral differences, retry state and final
  verification evidence.

GDI name and source ID are routing attributes, not physical identity. A stale,
missing or ambiguous physical target fails closed; VP must never write an old
baseline to an inferred replacement display.

Avoid `SDC_ALLOW_CHANGES` where possible. If Windows must alter additional
fields, compare the complete before/after topology and journal every observed
VP-induced difference before allowing the renderer to run.

When no renderer owns a field, a stable external user/topology change rebases
the neutral observation. Do not replay one startup snapshot indefinitely.

### 6. Observe settled display state before fresh construction

After renderer-local retirement and any final modeset:

- resolve the intended physical target again;
- require stable exact refresh/path observations;
- query available Advanced Color/HDR state using the exact target identity;
- report unexplained persistence of madVR-owned state without writing it; and
- construct the successor only from this newly queried state, never from an
  outgoing renderer snapshot.

VP does not claim authority over madVR-private settings or driver state it did
not mutate through a qualified public contract.

### 7. Qualify NVIDIA policy before production restoration

- Determine whether the pre-mutation state represents automatic/default policy
  or a concrete override on every supported NVAPI/driver version.
- Use the supported reset operation for automatic/default policy; replay only a
  proven concrete override.
- Re-resolve the exact physical display after the final modeset.
- Perform post-restore readback and retain the dirty journal on lookup failure,
  API failure, mismatch or unsupported semantics.
- Never create, erase or silently convert a persistent user override.

An API or driver readback is diagnostic evidence, not proof of the physical
HDMI/DisplayPort packet observed by the display.

## Fullscreen and graph-retarget rules

Fullscreen is application continuity, not renderer state.

- A madVR <-> VP Renderer selection while already fullscreen keeps the same
  validated fullscreen host HWND covered and performs the complete cold-start
  renderer replacement.
- Same-renderer rebuilds also use the complete cold-start boundary.
- The specialized madVR `GraphRetarget` remains an explicitly intra-generation,
  non-replacement operation for changing the presentation HWND. It may reuse
  the cover, host lease, admission and token discipline, but it does not satisfy
  renderer retirement and must not be used for DirectShow <-> non-DirectShow
  renderer selection.
- A failed retarget or rollback escalates to full renderer retirement and fresh
  construction.

The UI thread may request or observe a transition but must not wait for graph
teardown, a driver call, a modeset or a stability interval. Work and retries use
the lifecycle worker plus typed completion messages/timers, never a nested
message pump.

## Implementation sequence

### Slice A — cold-start prerequisites

- Separate DirectShow graph-event and owner-completion wake messages.
- Preserve a terminal retirement token while revoking new admission.
- Make DirectShow retirement success truthful and retryable.
- Add the failed-stop forced-unblock seam and fault-injection coverage.
- Remove maximum-mode mutation from topology restoration and verify exact
  restoration before clearing recovery.

### Slice B — renderer-neutral display journal

- Move refresh/NVIDIA recovery state out of the dead renderer object.
- Add physical-target identity and complete path-diff capture.
- Block successor construction independently on local retirement and dirty
  display journal barriers.

### Slice C — observation and NVIDIA qualification

- Add Advanced Color observation.
- Qualify reset/replay policy and final NVAPI readback on supported hardware.
- Add structured blocked-state diagnostics and retry evidence.

### Slice D — live acceptance

- Run and record the repeated fullscreen bidirectional hardware matrix.
- Treat process exit during a fault as a secondary fault-policy test, not the
  handoff mechanism or normal recovery plan.

## Source-informed design review

- [Microsoft `IMediaEventEx::SetNotifyWindow`](https://learn.microsoft.com/en-us/windows/win32/api/control/nf-control-imediaeventex-setnotifywindow)
  validates generation data in `lParam` and the drain-until-failure event loop.
- [MPC-HC `CloseMedia`](https://github.com/clsid2/mpc-hc/blob/develop/src/mpc-hc/MainFrm.cpp)
  demonstrates notification revocation and dedicated graph-message purging;
  VP must first separate its multiplexed notification/completion wakes.
- [mpv D3D11 context](https://github.com/mpv-player/mpv/blob/master/video/out/d3d11/context.c)
  demonstrates deliberate backbuffer/swapchain/device teardown. VP already
  follows the essential renderer-local release ordering; the remaining problem
  is cross-renderer application/display ownership.

## Verification plan

### Automated seams

- Admission-revocation tests proving stale state/detail/restart/graph-event work
  is rejected while the matching terminal retirement completion is accepted
  exactly once.
- Independent stale graph-event and owner-completion wake tests, including a
  purge that cannot remove terminal completion.
- DirectShow partial-build, failed `Stop`, timed-out `GetState`, blocked
  downstream `Receive`, OSD-release failure, incomplete teardown and retry
  success tests.
- Assertions that no successor is constructed until both completion barriers
  are clean.
- Exact topology restoration tests covering mode, refresh rational, position,
  scaling, rotation and path flags; the recovery record remains on mismatch.
- Display journal tests for query failure, hotplug, ambiguous target, topology
  reroute, collateral path changes, retry success and retry deadline.
- NVIDIA abstraction tests for automatic/default preservation, explicit
  override preservation, lookup/SET/reset failure and readback mismatch.
- Current beta baseline and changed x64 Release builds plus native and
  configuration/UI integration suites.

### Live fullscreen acceptance matrix

On supported NVIDIA hardware and at least one non-NVIDIA system where possible:

| Case | Required outcome |
| --- | --- |
| Start fullscreen; madVR -> VP Renderer -> madVR | Application never leaves fullscreen; the same validated host/physical target persists; each renderer generation, graph/device/swapchain, queue epoch, callback binding and notification registration is new. |
| Start fullscreen; VP Renderer -> madVR -> VP Renderer | Same result in reverse order. |
| Five fullscreen round trips | No stuck `Stopping`, missed terminal completion, stale callback, progressive resource leak or display drift. |
| Same-renderer rebuild | Complete cold-start boundary on the same covered fullscreen host. |
| Intra-generation madVR graph retarget | Guarded host transaction only; failure escalates to full cold rebuild. |
| Partial graph teardown or blocked `Receive` | No successor; cover and app remain alive; actionable blocked diagnostics and safe retry. |
| Refresh change, topology reroute or hotplug during retirement | No write to a stale target; dirty recovery retained until safe resolution. |
| NVIDIA automatic and explicit AVI policy | Qualified policy preserved and verified by readback; wire state explicitly reported as unverified without independent measurement. |
| Process exit during blocked recovery | Secondary fault policy is explicit and logged; no unsafe thread termination. |

## Acceptance criteria

- Current beta generation safety remains intact and the obsolete VP-0134 branch
  is never re-merged.
- DirectShow and VP Renderer replacements behave as fresh in-process starts
  while application/fullscreen continuity remains intact.
- Revoking outgoing admission cannot prevent its typed terminal retirement
  result from being accepted exactly once.
- DirectShow graph-event purging cannot remove lifecycle completion.
- DirectShow retirement reports success only when graph resources are proven
  released; failed stop cannot enter an unobservable unbounded drain.
- No incoming renderer exists until the renderer-local and display-global
  completion barriers are both clean.
- Exact topology restoration never applies an unjournaled maximum mode and
  never clears durable recovery after only target-membership validation.
- Topology/identity uncertainty fails closed and never restores a stale display
  baseline.
- The UI and fullscreen host remain alive and responsive while work, stability
  observation and retry run off the UI thread.
- Automated seams pass and the live matrix records successful repeated
  fullscreen bidirectional switching.

## Non-goals

- Restarting VideoProcessor as the normal renderer-switch mechanism.
- Leaving fullscreen merely to change renderer implementation.
- Resetting madVR-private settings, arbitrary driver state or effects of user
  scripts that VP did not own through a qualified public contract.
- Claiming Windows/NVAPI success proves the physical output packet.
- Replacing completed beta lifecycle work with the obsolete feature branch.

## Dependencies

- VP-0135 retains ownership of direct asynchronous execution of user-configured
  refresh commands. VP-0134 records ordering and does not terminate an already
  launched command.
- Existing `RendererRetirementService`, `RendererGenerationGate`,
  `RendererResetCoordinator`, `RendererTransitionModel` and
  `DisplayTopologySession` are the starting architecture. Extend/reconcile them
  rather than adding a competing fourth state authority.
- Every source slice starts from the then-current remote beta tip in a clean
  worktree and is built/tested as x64 Release before deployment consideration.
