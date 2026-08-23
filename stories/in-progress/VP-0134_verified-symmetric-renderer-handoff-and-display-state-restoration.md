# VP-0134: Complete symmetric renderer handoff and display-state restoration

## Status

In Progress — re-scoped from the current remote integration branch on
2026-08-23.

The original VP-0134 safety slice is already integrated in the current beta
tip, `origin/v1.2.001-beta` at `0fab90191a29c0d5e9cd22dae4e72217e7289661`.
It landed as `580d6483a` (**VP-0134 enforce safe renderer handoff boundary**)
and `5320d9d1c` (**VP-0134 tag DirectShow stop completion wake**). The old
`codex/vp-0134-renderer-handoff` branch is not an ancestor of beta and must
not be revived or merged again.

Subsequent beta work also moved both DirectShow and VP Renderer retirement,
including failed-restoration retries, to the renderer lifecycle worker. The
bounded refresh verification loop still waits for stable observations, but it
does so off the UI thread. That resolves the prior UI-stall concern.

This story now owns the remaining work needed to make the clean-handoff claim
meaningful and symmetric: one renderer-neutral display-state transaction,
strict ownership and target identity rules, observable validation, and a
repeatable live acceptance matrix. It does not claim that VP can reset private
madVR or driver state that public APIs cannot observe.

## User story

As a VideoProcessor user switching repeatedly between madVR and VP Renderer,
I want the incoming renderer to start only after the outgoing renderer's work
is quiescent and every VP-owned display/output mutation has either been
verified restored or explicitly failed closed, so a switch cannot inherit stale
callbacks, resources, timing, or VP-owned global display state.

## Remote-state audit

### Delivered on current beta

- Renderer callbacks, DirectShow notification wakes, graph events, detail
  updates, and restart requests carry a renderer generation and stale messages
  are rejected.
- DirectShow notification instance data carries the generation. The receiver
  drains actual events from `IMediaEventEx::GetEvent`; notification `wParam` is
  not treated as an event code.
- Capture ingress is closed and drained before renderer destruction.
- Renderer destruction is detached from the UI-owned shared pointer, then
  queued to `RendererRetirementService`. No successor is constructed until a
  durable successful completion is observed.
- A failed VP Renderer retirement retains the old object, reports a blocked
  handoff, and retries via the lifecycle worker after one second. DirectShow
  and VP Renderer now use the same retirement boundary.
- VP Renderer releases its renderer, swapchain, textures, D3D device, cache,
  and output resources before restoring VP-owned refresh state. Refresh
  restoration requires two exact rational observations. NVIDIA AVI restoration
  re-resolves the display ID after the modeset and retains ownership after a
  failed lookup or `SET`.

### Still not guaranteed

- There is no application-owned, renderer-neutral journal that spans both
  renderer families and records exactly which global fields VP changed.
- Target identity is not yet strong enough to prove that a saved baseline still
  belongs to the same physical display after a topology change or hotplug.
- Observable Advanced Color/HDR state is not compared at the handoff boundary.
- NVIDIA restoration does not yet qualify whether the captured state was an
  automatic/default policy or a concrete override, nor does it perform the
  required post-restore policy/readback decision.
- The integrated code has not completed the required repeated live handoff and
  shutdown matrix. A successful API call or driver readback is not proof of
  wire-level HDMI/DisplayPort signalling.

## Scope and ownership model

The handoff coordinator owns the transition. A renderer destructor is only the
mechanism that releases renderer-local resources; it is not the transaction
authority.

| State | Owner and required treatment |
| --- | --- |
| Graph, filters, madVR COM object, VP Renderer swapchain/D3D objects, queues, timers, callback work | Outgoing renderer generation. Stop admission, revoke/cancel generation-owned work, retire the object, and reject late work. Never copy these settings to the successor. |
| Notification window, fullscreen host, transition window, UI state | Dialog/application. Keep one valid host, revoke old graph notification, and publish only a terminal retirement result. |
| DisplayConfig timing/path, VP-triggered refresh change, NVIDIA AVI state | Renderer-neutral coordinator. Journal an immediately-dirty VP-owned mutation; restore only the recorded field on the re-identified target. |
| Windows Advanced Color/HDR, madVR-private state, user scripts, physical wire packets | Observe and diagnose. Do not reset unless VP recorded ownership and has a safe public API contract. |

## Required implementation

### 1. Introduce a renderer-neutral handoff coordinator

Add a dialog/application-owned `RendererHandoffCoordinator` (name is not
prescriptive). It must own:

- monotonically increasing transition and renderer generations;
- a physical-target identity: adapter LUID, source ID, target ID, GDI name,
  and monitor/device identity where available;
- a renderer-neutral observation captured before the first VP-owned mutation;
- a per-transition journal of *only* fields VP changed, with original value,
  target identity, mutation result, and dirty/verified status; and
- an asynchronous retry/deadline state plus structured diagnostics.

Do not store one startup snapshot and replay it indefinitely. When no renderer
owns a field, a stable external user or topology change rebases the neutral
observation. A target mismatch invalidates the journal and fails closed; it
must never write the old target's baseline to an inferred replacement display.

### 2. Make every renderer change use one state machine

Apply the same state machine to VP Renderer -> madVR, madVR -> VP Renderer,
same-renderer rebuild, failed construction, fullscreen host retarget, and
application shutdown:

1. Allocate a transition ID, invalidate the outgoing generation, and stop new
   capture admission.
2. Stop generation-owned delivery/worker work, wait for the existing ingress
   drain, and cancel only scheduled VP-owned actions. Do not terminate an
   already-launched user refresh command.
3. Disable DirectShow notifications, revoke the notify window, drain/purge
   queued old-generation wakes, and retire the outgoing graph or VP Renderer
   through `RendererRetirementService`.
4. Re-query topology and resolve the intended physical target. Refuse a stale
   or ambiguous target.
5. Restore journaled VP-owned display state. Observe exact rational refresh,
   target/path identity, and available Advanced Color state asynchronously.
6. After the final modeset and stable target observation, restore the recorded
   NVIDIA policy and verify the result by driver readback.
7. Publish terminal success only when every required journal entry is clean;
   then construct the successor with a new generation. On failure keep ingress
   closed, retain retryable state, and show an actionable blocked status.

The UI thread may request or observe transitions, but must never wait for
driver teardown, a modeset, or a stability interval. The existing lifecycle
worker is the execution boundary; the coordinator must use completion messages
or timers for retries rather than a nested message pump.

### 3. Complete DirectShow event and teardown discipline

Keep the current generation gate, and add deterministic coverage for every
teardown path.

- Register `IMediaEventEx::SetNotifyWindow` with the active generation in
  instance data. Treat its window message only as a wake to drain `GetEvent`.
- Before releasing the graph event interface, disable notification, clear the
  notify window, and discard queued notification messages for the retiring
  generation. The gate remains the final defense against a message that races
  with purging.
- Ensure a partial graph build, filter disconnect failure, stop timeout, or OSD
  removal failure cannot escape a destructor, leave a live callback target, or
  allow successor construction.
- Keep all COM graph/filter operations on their established owner apartment.
  Cross-thread work may wait for the owner completion, but must not directly
  release apartment-affine graph state from the wrong thread.

### 4. Define display and NVIDIA ownership precisely

- Mark a journal entry dirty immediately after any successful mutating display
  API call, not only after a later verification succeeds.
- Restore refresh through the current resolved path and require consecutive
  exact rational matches. A `SetDisplayConfig` success code alone is not
  acceptance.
- Record the NVIDIA policy kind before mutation (`automatic/default` versus
  explicit override), choose `RESET` or a replay only after supported-driver
  qualification, and perform a final readback. Never create, erase, or
  silently convert a persistent user override.
- Compare the observable Advanced Color/HDR target state before and after a
  transition. Report a mismatch without claiming to have restored a value VP
  did not own.

## Source-informed design review

The following public implementations inform the design; they are reference
patterns, not APIs that VP can adopt unchanged.

- [Microsoft's `IMediaEventEx::SetNotifyWindow` documentation](https://learn.microsoft.com/en-us/windows/win32/api/control/nf-control-imediaeventex-setnotifywindow)
  specifies that the window message has `wParam == 0`, carries the supplied
  instance data in `lParam`, and must be followed by draining `GetEvent` until
  it fails. This validates VP's generation-in-`lParam` approach and rules out
  treating the wake itself as an event.
- [MPC-HC `CloseMedia`](https://github.com/clsid2/mpc-hc/blob/develop/src/mpc-hc/MainFrm.cpp)
  disables graph notifications, clears the notify window, releases the event
  interface, and purges queued graph notifications during close. VP should
  retain generation gating as a stronger final guard, while adopting the same
  explicit revocation/purge ordering.
- [mpv's D3D11 context](https://github.com/mpv-player/mpv/blob/master/video/out/d3d11/context.c)
  refuses to resize while a backbuffer is acquired and tears down exclusive
  fullscreen, backbuffer, swapchain, window binding, device, and graphics
  abstraction in a deliberate order. That supports VP's rule that the outgoing
  swapchain/device must be fully released before restoring display-global state
  or constructing the successor.

## Verification plan

### Automated seams

- Unit tests for generation rejection across state/detail/restart/graph-event
  messages and a stale DirectShow wake after notification revocation.
- Coordinator tests for: normal completion; query failure; mismatched rational
  rate; target identity change; hotplug; ambiguous target; user topology change
  while neutral; retry success; retry deadline; and successor-construction
  refusal while any required entry is dirty.
- Fault-injection tests for DirectShow partial teardown and OSD failure. Verify
  that the object stays owned by the retirement service and no live callback is
  accepted after terminal completion.
- NVIDIA abstraction tests for lookup failure, `SET` failure, readback mismatch,
  automatic-policy preservation, and explicit-override preservation.
- Build the current remote beta baseline and run the native test suite plus the
  configuration/UI integration suite before and after this work.

### Live acceptance matrix

On supported NVIDIA hardware and at least one non-NVIDIA system where possible,
record diagnostic evidence for each case:

| Case | Required outcome |
| --- | --- |
| VP Renderer -> madVR -> VP Renderer | No stale callbacks, blocked-state leak, queue carry-over, or unverified VP-owned display state. |
| madVR -> VP Renderer -> madVR | Same result in reverse order. |
| Five repeated round trips | No stuck `Stopping`, missed retirement completion, or progressive resource/display drift. |
| Same-renderer rebuild and fullscreen retarget | Same transaction and a valid window/swapchain owner. |
| Quit during normal handoff and during failed restoration | Clean process exit or explicit, logged fail-closed result; no forced termination. |
| Refresh change, topology change, and target hotplug during retirement | No write to a stale target; successor remains blocked until a safe resolution. |
| NVIDIA automatic and explicit AVI policy | Post-restore driver readback matches the qualified policy; wire state is reported as unverified unless independently measured. |

## Acceptance criteria

- The current beta generation-safe retirement implementation remains intact;
  no stale VP-0134 feature branch is re-merged.
- Every renderer replacement and shutdown path uses the coordinator state
  machine and lifecycle-worker completion boundary.
- No new renderer exists while the previous generation or a required
  VP-owned display-state journal entry remains unresolved.
- The UI remains responsive during driver teardown, stability checks, and
  retry delays.
- Topology or ownership uncertainty fails closed and never restores a stale
  display baseline.
- All automated seams pass and the live matrix records successful bidirectional
  repeated-switch and quit behavior.

## Non-goals

- Resetting madVR-private settings, private driver state, or arbitrary effects
  of user scripts.
- Claiming that a Windows API result or NVIDIA readback proves the physical
  output packet observed by a display or projector.
- Replacing the completed generation-safety/lifecycle-worker implementation
  with the obsolete `codex/vp-0134-renderer-handoff` branch.

## Dependencies

- VP-0135 retains ownership of direct asynchronous execution of user-configured
  refresh commands. VP-0134 records ordering and does not cancel a command
  already launched under that contract.
- Current beta renderer retirement and DirectShow lifecycle work are the base
  implementation. Start follow-on source changes from the then-current remote
  beta tip in a clean worktree.
