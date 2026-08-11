# VP-0115: Diagnose DeckLink delivery failures and renderer-transition drops

## Status

Backlog (2026-08-11). Created from the deployed `vp.log` review for 2026-08-10. No runtime behavior has changed.

## User story

As a VP operator using a DeckLink capture device, I want delivery failures, buffer-acquisition failures, queue overflows, and renderer-transition errors to be distinguishable and recover safely, so transient format/fullscreen changes do not silently cost frames or leave the renderer in a degraded state.

## Observed evidence

`C:\Videoprocessor\vp\logs\vp.log` on 2026-08-10 recorded isolated examples of:

- `OnVideoFrame: Raw queue OVERFLOW` with one dropped frame;
- `DELIVERY THREAD: Deliver() failed` with `0x80004005`;
- `CONVERSION WORKER: GetDeliveryBuffer FAILED` with `0x80040211`;
- DirectShow `SetWindowPosition` failures and a fullscreen retarget rollback caused by an empty target rectangle.

The last Alpha session in that log initialized, presented, and shut down cleanly; this story must not assume that every recorded error is an Alpha renderer defect. The investigation must associate each event with the capture device, source format change, renderer, graph/reset generation, fullscreen state, and teardown/restart phase before deciding whether it is expected cancellation noise, a recoverable transient, or a real frame-loss defect.

## Scope

1. Reproduce representative DeckLink input, signal-format, renderer-switch, fullscreen-retarget, and shutdown sequences while preserving timestamped evidence.
2. Trace the affected boundaries from DeckLink callback through raw queue, conversion worker, DirectShow output pin, renderer delivery, and reset coordination.
3. Classify HRESULTs and failures by lifetime state: normal teardown/cancellation, stale generation, temporary backpressure, malformed/empty presentation target, or actionable fault.
4. Add bounded diagnostics that correlate a failed delivery with frame number, queue depth/capacity, capture and renderer generation, graph state, renderer identity, transition reason, and the target rectangle when applicable.
5. Make only evidence-backed improvements: avoid delivery attempts during a known invalid transition, retry/re-prime only when ownership and generation make it safe, and preserve the existing low-latency policy.

## Acceptance criteria

- A repeatable trace or test matrix covers normal playback, input resync/format change, Alpha and DirectShow renderer changes, fullscreen enter/exit, and orderly shutdown.
- Each relevant failure is classified as expected lifecycle noise, recoverable transient, or defect with a documented reason and HRESULT interpretation.
- Delivery and buffer failures contain enough correlated state to identify the frame/generation/transition that caused them without enabling unbounded logging.
- Empty fullscreen targets are rejected before a DirectShow window-position operation, or the transition is safely deferred with a clear diagnostic.
- Any intentional drop during reset/teardown is counted and reported separately from unexplained playback loss.
- A proposed mitigation demonstrably reduces unexplained `Deliver`, `GetDeliveryBuffer`, or raw-queue-overflow events without introducing stale-frame presentation, deadlock, extra queue growth, or a regression in Alpha/DirectShow renderer switching.
- Focused tests and a clean x64 Release build pass; live validation includes DeckLink playback through a format or fullscreen transition.

## Non-goals

- Replacing the DeckLink capture architecture or implementing the native SDK capture-path investigation in VP-0068.
- Treating expected cancellation during an orderly shutdown as a user-visible error.
- Increasing queue sizes or adding arbitrary retries merely to hide a timing/ownership defect.

## Dependencies and references

- VP-0046: DirectShow event plumbing and passive health diagnostics.
- VP-0061 and VP-0084: DirectShow reset/re-prime and steady-queue work.
- VP-0068: native Blackmagic SDK capture-path feasibility and metadata contract.
- Deployed log evidence: `C:\Videoprocessor\vp\logs\vp.log`, 2026-08-10.
