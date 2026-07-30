# VP-0066-3: Extract epoch-aware frame transport and processing components

## Status

Backlog. Depends on accepted VP-0066-2 timing contracts and replay evidence.

## Parent and dependency

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

Dependency: [VP-0066-2](VP-0066-2_extract-graph-independent-video-timing-controller.md).
VP-0066-4 may begin only after this task is accepted.

## Objective and scope

Extract `CaptureFrameQueue`, `ProcessedFrameQueue`, `FrameProcessor`, and the
neutral raw/processed frame contracts. Carry the common `PipelineEpoch`, source
frame number, capture time, format and analysis metadata, and processing
duration across the processing boundary. Preserve the existing two queue
locations, ownership transfer, overflow policies, event signaling, and worker
handoff count exactly; this task must not add a queue or frame copy.

`FrameProcessor` owns conversion and active-picture/subtitle/scene/HDR-SDR
analysis, but not final timestamps, correction decisions, or downstream
delivery. The queues own their frames/samples and support deterministic
flush/stop, current-epoch admission, stale discard, and structured depth/
overflow/wait metrics.

## Acceptance criteria

- Focused tests prove queue depth limits, overflow policy, ownership release,
  wake/stop/flush behavior, and stale-epoch rejection/discard for both queues.
- Processor tests prove captured-frame metadata is retained through conversion
  and that it neither creates final timestamps nor calls downstream delivery.
- A deterministic processing-path test using fakes/fixtures proves source
  order, epoch propagation, processing-failure handling, queue metrics, and
  no stale result reaches the delivery boundary.
- VP-0066-1 trace replay remains equivalent through the newly extracted
  boundaries, with no undocumented queue-policy, format, analysis, or timing
  behavior difference.
- Measured queue depth, handoff count, copies, and capture-to-delivery latency
  remain within the VP-0066 baseline guardrail.

## Out of scope

Applying decisions to `IMediaSample`, downstream `Deliver()`, and coordinated
DirectShow reset/shutdown. Those system-boundary responsibilities remain for
VP-0066-4.
