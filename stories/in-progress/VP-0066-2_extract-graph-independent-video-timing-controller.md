# VP-0066-2: Extract a graph-independent video timing controller

## Status

In Progress (2026-07-30). VP-0066-1's self-identifying baseline is accepted
for extraction, including the 24000/1001 capture-to-monitor cadence-mismatch
fixture. Display-refresh telemetry remains an explicit optional follow-up.

## Parent and dependency

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

Dependency: [VP-0066-1](VP-0066-1_characterize-live-output-golden-traces.md).
VP-0066-3 may begin only after this task is accepted.

## Objective and scope

Extract `PipelineEpoch`, value-type `FrameTimingInput`, `TimingDecision`, and
`VideoTimingController`. Move the existing clock/synthetic timestamp,
smoothed-duration, rational remainder, monotonicity, PPM, queue-depth,
display-phase, and scene-aware correction logic into that controller without
behavior change. Keep the existing timestamp-mode names through a compatibility
adapter while equivalent behavior is proven.

The controller must not own queues or samples, analyze frames, call
`IMediaSample::SetTime`, or call DirectShow delivery APIs. It receives values
and returns a decision; the existing pipeline remains responsible for applying
it until VP-0066-4.

## Acceptance criteria

- Unit tests run without a DirectShow graph, renderer, capture device, worker
  thread, or real `IMediaSample`.
- Tests cover exact 24000/1001 and 60000/1001 timing, rational accumulation,
  monotonicity, positive/negative PPM, capture faster/slower than display,
  queue growth/depletion, scene-aware correction, invalid/missing timestamps,
  missing display rate, reset, and epoch replacement.
- Four-hour simulations at both exact rates keep timestamp error bounded and
  preserve the intended rational remainder behavior.
- Replaying VP-0066-1 fixtures produces equivalent timestamp and correction
  decisions within the recorded tolerances; any difference is documented and
  explicitly approved.
- The compatibility adapter preserves all existing timestamp modes and this
  extraction introduces no queue, copy, worker handoff, or buffering change.

## Out of scope

Queue/processor extraction, DirectShow sample delivery, and coordinator
lifecycle changes, which remain for VP-0066-3 and VP-0066-4.
