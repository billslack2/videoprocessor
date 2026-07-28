# VP-0043: madVR graph re-prime after lifecycle and queue pressure

## Status

Backlog. No implementation has started.

## User story

As a madVR/DirectShow user, I want VP to perform one bounded complete graph
re-prime after a renderer or display transition, and after sustained queue
pressure, so madVR's internal queues recover even though madVR does not expose
`IQualityControl` feedback to VP.

## Reported evidence

During an Alpha-to-madVR switch on July 28, 2026, the OSD and madVR overlay
showed a usable but underfilled pipeline:

- VP queue: `0/1/1/32` (raw/converted/total/capacity)
- madVR decoder/upload/render queues: approximately `1-2 / 8`
- madVR present queue: `2-3 / 3`
- no reported drops; conversion average approximately 1.8–2.1 ms

The active deployed log records:

```text
16:15:43 Reset scheduled: reason=post-renderer-start scope=live-queue delay=3000ms
16:15:46 Reset executing: reason=post-renderer-start scope=live-queue
16:15:46 DirectShowVideoRenderer::ResetLiveQueue() - flushing live source queue only
16:15:46 ... queues/timing reset, buffering enabled, new segment delivered
```

The three-second timer therefore fired at the intended time, but only reset
VP's live-source queues. It did not stop/restart madVR's DirectShow graph.

Manual `R` recovered the condition and logged a materially different action:

```text
16:16:05 Reset executing: reason=manual scope=graph
16:16:05 DirectShowVideoRenderer::Reset() - Stopping graph for complete restart
16:16:05 DirectShowVideoRenderer::Reset() - Graph restarted
```

This demonstrates that the recovery required by this transition is the full
graph reset, not a longer wait before a live-source-only reset.

## Problem

VP cannot observe madVR's decoder, upload, render, or presentation health via
`IQualityControl`. Its own queue is the available fail-safe signal, but it
must not treat every empty/low queue as failure: a live pipeline can
legitimately keep pace with the source at low depth.

The current lifecycle coordinator schedules `post-renderer-start` recovery as
`ResetLiveQueue()`. That operation resets VP buffering/timing only. It cannot
make a newly attached madVR graph rebuild and prefill its internal queues.

## Required reset policy

Apply this policy to the DirectShow/madVR backend only. Alpha retains its
native queue lifecycle unless separately changed by an Alpha story.

| Trigger | Required recovery |
| --- | --- |
| DirectShow renderer start or handoff into DirectShow/madVR | One delayed complete graph reset, equivalent to manual `R` |
| Confirmed display/video-mode lifecycle transition while DirectShow is active | One delayed complete graph reset |
| Sustained VP raw or converted queue high-water/full pressure | One debounced complete graph reset |
| Brief low/empty VP queue | Diagnostics only; no automatic reset |
| Manual `R` | Existing complete graph reset |

A complete graph reset already stops/restarts the graph and resets the live
source. Do not add a redundant queue-only reset before or after it unless
testing proves a specific ordering requirement.

## Required design and safeguards

1. Use the existing `RequestRendererReset` coordinator rather than creating
   competing timers or reset paths.
2. Make a DirectShow `post-renderer-start` request graph-scoped. The existing
   configured queue-reset delay remains the delay before this one controlled
   re-prime.
3. Treat a renderer handoff into DirectShow exactly as a new DirectShow graph,
   including Alpha-to-madVR and other renderer-to-madVR switches.
4. Make confirmed DirectShow display transitions graph-scoped. Coalesce a
   burst of display/window notifications into one reset.
5. Restore queue-pressure recovery as a graph-scoped failsafe only after the
   existing high-water persistence threshold is met. It must not trigger from
   a transient high sample or from low/empty queue depth.
6. Preserve one pending-reset record with a clear dominance order: graph reset
   dominates live-queue reset, and an explicit lifecycle transition dominates
   a generic pressure reason for diagnostics.
7. After execution, use a bounded cooldown/backoff. A persistent real
   bottleneck may reappear after a complete reset; log that fact rather than
   repeatedly tearing down the graph in a reset loop.
8. Do not rely on a fixed longer delay as the primary repair. The log evidence
   shows the existing three-second timer executed; the missing operation was
   graph reconstruction.
9. Do not make normal NLS/profile, subtitle, OSD, or queue telemetry updates
   create duplicate graph resets. They must use the same coalescing contract.
10. Preserve the existing LLDV/EOTF lifecycle handling. A corrective graph
    reset must rebuild using current effective capture metadata and must not
    resurrect a stale HDR state.

## Required diagnostics

For each request and execution, log:

- renderer backend/name and renderer generation;
- reason, scope, delay, coalesced/superseded reason, and cooldown state;
- queue raw/converted/current capacity and high-water duration;
- display transition or renderer-handoff identity where applicable;
- graph stop/start completion for graph-scoped resets;
- first post-reset buffering completion and queue state;
- periodic post-reset health summaries sufficient to show recovery or a
  persistent downstream bottleneck.

Make it clear in logs whether the operation was `live-queue` or `graph`; this
distinction directly explains why manual recovery may differ.

## Verification

1. Reproduce the Alpha-to-madVR switch with the 32-frame DirectShow capacity
   and verify the delayed post-start reset logs `scope=graph`.
2. Repeat madVR-to-Alpha-to-madVR switching at least 25 times, including
   fullscreen/windowed transitions where supported. Verify madVR queues fill
   normally without manual `R`.
3. Test DirectShow startup, manual renderer restart, display-mode/refresh-rate
   switch, EOTF/LLDV transition, and source change. Confirm exactly one
   coalesced graph re-prime per lifecycle event.
4. Test transient high queue use, sustained high-water/full pressure, and
   ordinary empty/low queues. Only sustained pressure may trigger automatic
   graph recovery.
5. Simulate or reproduce an unrecoverable downstream stall. Confirm bounded
   cooldown/backoff prevents a reset loop and logs the persistent condition.
6. Confirm source timing restarts cleanly, no stale samples cross the reset
   boundary, and no extra drops, audio/video desynchronization, or HDR-state
   regression appears.
7. Confirm Alpha renderer queue behavior remains unchanged.

## Acceptance criteria

- The delayed post-start reset after entering DirectShow/madVR is equivalent
  to manual `R` in scope: it performs one complete graph reset.
- A sustained high-water condition can recover madVR through one complete
  graph reset even without `IQualityControl` feedback.
- Low/empty VP queue states do not cause automatic reset loops.
- Transition and pressure reset paths are coalesced, bounded, and clearly
  logged.
- Repeated Alpha/madVR handoffs leave madVR's internal queues normally filled
  without manual intervention.
- DirectShow timing, LLDV/EOTF behavior, queue accounting, NLS, and Alpha
  playback do not regress.

## Dependencies

Builds on VP-0013's DirectShow queue/reset alignment and the current reset
coordinator. It is a targeted correction to the accepted lifecycle policy,
based on post-release live evidence.

