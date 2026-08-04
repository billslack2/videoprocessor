# VP-0073: Diagnose and minimally repair keyboard-command responsiveness

## Status

Done. Accepted by the user after live Alpha/madVR handoff testing and merged
into `v1.1.015-beta` through
[videoprocessor PR #37](https://github.com/billslack2/videoprocessor/pull/37)
as merge commit `99df2db` on 2026-08-04 (feature commit `fcb80c3`).

The confirmed defect was missing DirectShow child-window keyboard routing:
the renderer video child had no `IVideoWindow::put_MessageDrain`, so VP's
accelerator table could be bypassed while that child owned input during a
renderer handoff. The repair routes DirectShow keyboard messages to the VP
dialog and clears that route during teardown; it does not change renderer
lifetime, queue, reset, epoch, or presentation policy.

## Completion evidence (2026-08-04)

- A clean x64 Release build on the current integration base succeeded and all
  536 native tests passed.
- Repeated live Alpha/madVR switches kept Ctrl+I responsive. Telemetry showed
  every exercised Ctrl+I reaching VP and being consumed by its accelerator in
  0-16 ms.
- Live Alt+F4 reached the fullscreen host, forwarded to the main VP window,
  and initiated shutdown without requiring Alt+Tab.
- The instrumented DirectShow transitions completed graph stop and teardown
  in 437-438 ms during acceptance; renderer retirement then completed
  immediately. The earlier approximately eight-second transition did not
  recur, and phase telemetry remains available if it does.
- The user reported the repaired build worked much better and explicitly
  accepted VP-0073 as done after the merged build passed the exercised
  renderer-switch and command-routing scenarios.

## User story

As a VideoProcessor user, I want Alt+F4 and normal VP keyboard commands to
remain responsive during renderer, reset, refresh, and epoch transitions, so I
can close or control VP without waiting for a stalled UI operation to finish.

## Reported behavior

At times Alt+F4 and other keyboard commands appear not to work. The trigger is
not proven. Suspects include renderer switching, reset/re-prime, epoch/display
transitions, or OSD/focus interaction. VP-0071 is not implemented and must
not be presumed to be the cause.

## Investigation question

Classify each reproduced failure as exactly one of:

1. VP did not receive the key/system command because focus or another window
   owned it.
2. VP received it but an accelerator, hook, child/overlay window, or command
   router consumed it.
3. VP received it but its UI thread was blocked and could not dispatch it.
4. VP dispatched it but its handler intentionally deferred, rejected, or
   deadlocked during renderer/reset lifecycle work.

Do not attribute it to queues, Alpha, madVR, or OSD without evidence.

## Scope and constraints

- Start with lightweight observability and reproduction. The expected result
  may be a narrow focus/routing repair, not renderer work.
- Instrument `WM_KEYDOWN`, `WM_SYSKEYDOWN`, `WM_COMMAND`, `WM_SYSCOMMAND`
  (especially `SC_CLOSE`), handler entry/exit, and focus/activation changes.
  Include message age, focused/foreground HWND, renderer generation/backend,
  reset/epoch/display state, and UI-dispatch heartbeat age.
- Log command/lifecycle state changes and proven dispatch stalls only; never
  per-frame or every normal message.
- Make no broad queue, timing, epoch, renderer, or OSD redesign. Select the
  smallest proven repair: prevent focus stealing, stop consuming an unowned
  system key, or move a bounded synchronous wait off the UI thread.
- Alt+F4/`SC_CLOSE` must remain an escape route. Do not suppress it to avoid a
  shutdown race; make shutdown generation-safe.

## Required test matrix

| Scenario | Required result |
| --- | --- |
| Stable playback, OSD hidden/shown | Alt+F4 and VP shortcuts reach intended handler promptly |
| Alpha <-> madVR switch | Focus/routing remains valid; UI dispatch does not stall |
| Manual reset and automatic re-prime | Input remains dispatchable while work is pending/coalesced |
| Refresh/display and HDR/SDR transition | `SC_CLOSE` works safely without indefinite wait |
| Windowed, fullscreen-windowed, exclusive | Focus ownership and close behavior are explicit |
| Repeated input during stall | Logs classify receive/consume/block/defer without flood |

## Acceptance criteria

- Logs conclusively classify a reproduced failure as missing, consumed,
  UI-blocked, or handler-deferred.
- Alt+F4 closes VP safely and normal shortcuts remain usable across exercised
  transitions, or a documented OS focus limitation is identified.
- The fix is narrow and generation-safe, without reset loop, drop burst,
  queue change, renderer restart, or UI regression.
- Alpha, madVR, no-renderer, and OSD visible/hidden behavior remain covered;
  VP-0071 remains independent.

## Related work

- VP-0066; VP-0061; VP-0063; VP-0071.
- `src\\VideoProcessor-GUI\\VideoProcessorDlg.cpp`: message routing, renderer
  transitions, reset coordination, and UI ownership to inspect.
