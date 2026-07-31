# VP-0066-3: Extract epoch-aware frame transport and processing components

## Status

In Progress (2026-07-30). VP-0066-2 is accepted after a fresh x64 Release
build and 289/289 native tests. This task begins with behavior-preserving
extraction only: it must preserve the existing two queues, ownership transfer,
worker handoffs, event signaling, and overflow behavior before any startup or
output-readiness policy is considered.

Implementation checkpoint (2026-07-30): `EpochBoundedQueue` is now a
graph-free C++14-compatible transport primitive. `CaptureFrameQueue` owns the
existing raw `VideoFrame` source-buffer reference, and `ProcessedFrameQueue`
owns the existing converted `IMediaSample` reference plus source/capture/
processing/scene metadata. The buffered pin uses both in place of its inline
deques: capacity remains the configured VP queue size, raw overflow still
drops the oldest frame, converted trimming remains in the delivery path,
capture-to-conversion and conversion-to-delivery remain the same two
event-driven worker handoffs, and reset/activation establish the authoritative
`PipelineEpoch` before stale work is admitted. Historical safe-scene tagging
still operates only while conversion and delivery publication are serialized.

`FrameProcessor` now owns the conversion invocation, conversion-duration
measurement, and neutral processed-frame result. It is graph-free in the
sense that it has no graph, queue, worker, timestamp, cadence, or delivery
dependency; a pin-supplied conversion callback retains the current formatter
and image-analysis implementation until that analysis is separately moved with
equivalent tests.

Focused ownership/overflow/stale/resize/flush/cushion/scene-tag and processor
tests pass, as does the x64 Release native suite (297/297). The DirectShow
timestamp, delivery, flush, and lifecycle paths are intentionally unchanged at
this checkpoint.

Live validation (SDR 60000/1001, 2026-07-30): the deployed transport and
processor checkpoint reproduced the expected post-reset VP reserve. With
input 59.940060 Hz, display 59.950499 Hz, delivery 59.941079 Hz, and PPM -17,
the converted queue held 8 frames initially and then 9--10 frames for roughly
35 seconds; raw depth remained 1. The retained pre-reset trace showed the
known 21--22-frame startup accumulation. Seven `E_FAIL` delivery completions
occurred together at inactive shutdown, not during steady playback. No trace
records were lost, Rational/Rational remained 2,470/0 applied/mismatched, and
the user observed no behavioral regression. As designed, the run provides no
structured madVR occupancy evidence.

Active-picture analyzer checkpoint (2026-07-30): `ActivePictureAnalyzer` now
owns P010 evidence extraction, sparse analysis scheduling, and the existing
worker-owned confidence/hysteresis model. The pin remains only the DirectShow
sample/media-type adapter and the publisher of UI-visible active-picture
state. The existing conversion worker, queue ownership, frame cadence, and
delivery path were not changed. A fresh x64 Release build passed 305/305
native tests; the user then ran the deployed build and reported normal,
unchanged behavior. Scene and subtitle analysis remain the next processing
seams before this task can be accepted.

Scene-analysis checkpoint (2026-07-30): `FrameProcessor` now owns the
validated P010 scene-detector invocation and returns only boundary/average
luma metadata. The buffered pin remains the DirectShow sample/media-type
adapter and preserves its existing process-wide event-ID publication, counters,
generation reset, and queue tagging. A fresh x64 Release build passed 306/306
native tests, and the user reported the deployed build looked normal.

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
