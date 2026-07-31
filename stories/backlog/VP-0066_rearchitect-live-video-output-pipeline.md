# VP-0066: Re-architect the live video output pipeline into testable components

## Status

In Progress (2026-07-30). This is a large, behavior-preserving architecture
story. VP-0066-1 and VP-0066-2 are accepted; VP-0066-3 may now extract the
queue/processor seam. The root remains incomplete until all four child tasks
and the cross-component latency guardrail pass.

## Decomposition

This root story is a completion roll-up; implementation proceeds through these
ordered, independently testable tasks rather than through unverified internal
class extractions:

1. [VP-0066-1](VP-0066-1_characterize-live-output-golden-traces.md) — capture
   the current behavior in replayable golden traces and latency measurements.
2. [VP-0066-2](VP-0066-2_extract-graph-independent-video-timing-controller.md)
   — extract and unit-test timing as a graph-independent component while
   comparing decisions with the baseline traces.
3. [VP-0066-3](VP-0066-3_extract-epoch-aware-frame-transport-and-processing.md)
   — extract the frame queues and processor with ownership, overflow, epoch,
   and processing tests.
4. [VP-0066-4](VP-0066-4_integrate-directshow-delivery-and-lifecycle.md) —
   integrate DirectShow delivery, workers, reset/shutdown coordination, and
   whole-pipeline regression validation.

Each task depends on the preceding task's accepted evidence. This root may
move to `Done` only after all four children are done and its complete
cross-component acceptance criteria and latency guardrail have passed.

## User story

As a developer maintaining the live DirectShow video pipeline, I want capture
buffering, frame processing, timestamp generation, cadence correction, and
DirectShow delivery separated into clearly defined components, so timing
behavior can be tested independently and changes to one stage cannot
unintentionally affect the rest of the pipeline.

## Background and objective

`CBufferedLiveSourceVideoOutputPin` currently combines captured-frame intake,
raw and converted queues, pixel conversion, active-picture/subtitle/scene
analysis, HDR/SDR metadata handling, timestamp generation, rational timing,
PPM and queue correction, drop/repeat planning, reset/flush/generation state,
DirectShow delivery, and pipeline statistics. This makes timing changes hard
to reason about and requires a complete DirectShow graph to test much of the
behavior.

Refactor the pipeline into independently testable components without
intentionally changing runtime behavior. Preserve existing capture,
conversion, timestamp, buffering, delivery, scene-aware correction,
SDR/HDR-transition, and configuration behavior. Separate capture time from
scheduled presentation time, preserve queue policies and ownership, carry a
common pipeline epoch through all work, and provide diagnostics that compare
the old and new implementations.

A poorly implemented refactor could add latency if it introduces:

Another queue between components
Another worker-thread handoff
Frame copies instead of ownership transfer
Lock contention
Polling instead of event signaling
Batch processing
Conservative minimum queue depths
Waiting for both queues to reach a threshold
Logging or statistics on the critical path

One additional queued frame would add roughly:

16.7 ms at 59.94
41.7 ms at 23.976

An extra function call, virtual interface, or small value-object construction is effectively irrelevant compared with a frame interval. An extra queue or wait is not.

The refactor must introduce no additional frame queues, frame copies, worker-thread transitions, startup preroll, or minimum buffering requirements. Capture-to-delivery latency must remain within one millisecond of the existing implementation under equivalent queue conditions.

Also measure these before and after:

Capture-to-conversion-start latency
Conversion duration
Converted-queue residence time
Capture-to-Deliver() latency
Raw and converted queue depths
End-to-end capture-to-screen latency, where measurable
Expected result
Stage	Expected latency effect
Behavior-preserving refactor	Approximately 0 ms
Cleaner ownership and less locking	Possibly a very small reduction
Removing one buffered frame afterward	−16.7 ms or −41.7 ms
Accidentally adding one buffered frame	+16.7 ms or +41.7 ms
Better queue-depth control	Potentially meaningful reduction
New interfaces/classes alone	Negligible

So I would describe it as:

Latency-indifferent initially, but latency-enabling afterward.

The refactor should not itself be sold as a latency improvement. It gives you the structure and instrumentation needed to reduce latency without destabilizing timing.

## Target architecture

```text
Capture callback
    -> CaptureFrameQueue
    -> FrameProcessingWorker / FrameProcessor
    -> ProcessedFrameQueue
    -> VideoTimingController
    -> DirectShowFrameDeliverer
    -> downstream renderer
```

`CBufferedLiveSourceVideoOutputPin` remains the DirectShow-facing coordinator
for pin lifecycle, worker startup/shutdown, component ownership, coordinated
resets, high-level data flow, and fatal downstream errors. It should no longer
contain the detailed implementation of every stage.

## DirectShow and madVR boundary

madVR is a required DirectShow downstream renderer for this path. VP must
continue to deliver `IMediaSample` instances through the DirectShow output-pin
contract; this refactor does not replace that boundary with MPV, Media
Foundation, or a renderer-neutral presentation API.

madVR's configured internal CPU/GPU queues are opaque to VP. VP has no usable
`IQualityControl` or queue-depth feedback from madVR, and must not estimate
downstream occupancy from `Deliver()` timing. Record the configured madVR queue
settings as mode/test metadata, continuously feed it from the VP-owned queues,
and treat delivery duration, allocator waits/failures, and successful delivery
only as one-sided diagnostics. Queue-depth control and `queue error` in this
story always mean VP-owned raw/processed queue state.

## Live-capture determinism requirement

This is a live-capture pipeline. It does **not** need to converge to the
incoming live edge after a graph start, reset, or format change. It may instead
intentionally establish a fixed capture-to-presentation latency, provided that
the result is deterministic and consistent across equivalent starts.

The desired post-refactor behavioural improvement is a static, testable
startup/prefill policy, not a dynamic renderer-feedback controller. Its
configuration must distinguish:

- `vp_reserve_frames`: the minimum current-epoch converted-frame reserve VP
  retains for its own jitter protection;
- `presentation_lead_frames` (or an equivalent millisecond latency budget):
  the future presentation lead VP schedules downstream, which gives madVR a
  deterministic opportunity to prefill; and
- an explicit per-rate profile or a canonical latency budget. A shared frame
  count has materially different latency at 59.94 and 23.976 Hz, so the
  selected effective frame count and milliseconds must be logged for every
  run.

The policy must be finite-state and have one owner: the existing
DirectShow/coordinator delivery path. Processing workers may publish their
ordinary queue depth and carry an epoch tag, but must not independently advance
startup state, initiate a reset, or wait for another worker to reach a target.
`PipelineEpoch` is a stale-work/lifetime boundary, not a new cross-thread
coordination protocol. This preserves the current worker and flush ownership
model and avoids reintroducing the prior epoch/thread-lifetime failure mode.

HDMI output readiness is not equivalent to a DirectShow graph having entered
`Run`. Different displays and projectors can complete their HDMI handshake at
materially different times. Delivering live frames before that physical
handshake has settled causes a variable, accidental downstream backlog and
therefore non-deterministic madVR queue depth.

The intended state sequence is: wait for VP's output-readiness predicate, one
serialized post-ready flush/reset, static VP preroll, timestamp-lead
establishment, then steady delivery. It must begin after the last known
renderer/display-mode/output transition (not merely process start). It is a
VP-owned policy, not a user-configurable timeout. At minimum, the predicate
must require an observed madVR/display refresh rate that belongs to the
requested valid refresh family and remains stable across a bounded observation
window, with the renderer graph operational. Both the expected family and each
observed rate/stability transition must be logged.

During `OutputNotReady`, VP must not retain an unbounded or variable-age
backlog for later delivery: it may safely discard live capture because catching
up to the live edge is not a requirement. The post-ready flush must discard
any work that arrived before readiness; only frames accumulated after that
boundary may form the exact configured preroll. This is what makes the initial
queue depth independent of handshake duration.

No Windows, DirectShow, madVR refresh-rate, frame-grab, or successful
`Deliver()` signal by itself proves that a physical HDMI sink has completed its
lock/handshake. They provide a deterministic renderer-readiness gate, not
proof of projector/AVR lock. If true physical-lock proof is required for a
specific output chain, VP needs a separately validated hardware/vendor signal
(for example a projector/AVR or GPU-driver integration); it must not disguise
an arbitrary delay as proof. Until then, a failed or unstable readiness
predicate keeps VP in `OutputNotReady` with diagnostic reason/state rather
than releasing a variable backlog.
Once steady, the policy must not chase madVR queue depth, infer it from
`Deliver()` time, add/repeat live frames, or alter cadence correction.
The existing timestamp/queue path remains unchanged until this policy is
implemented in a separately approved, testable task after the
behaviour-preserving refactor.

For validation, use passive madVR OSD evidence only. PC madVR's in-process
`IMadVRFrameGrabber` can capture a frame including its OSD, but does not expose
structured queue occupancy; captured/OCR'd values are test evidence only and
never an input to live timing control.

## Required components

### `CaptureFrameQueue`

Own raw `VideoFrame` instances and capture buffers. Enforce the existing
maximum depth and overflow policy, signal processing, expose depth and
overflow statistics, support deterministic flush/stop, and reject stale
pipeline epochs. It must not allocate DirectShow samples, convert pixels,
generate timestamps, know about the renderer, or make cadence decisions.

### `FrameProcessor`

Process one captured frame and return a neutral `ProcessedFrame` containing the
converted sample and metadata needed later. It may perform required UYVY,
V210, P010, and other conversions; active-picture, subtitle, and scene
analysis; HDR/SDR metadata processing; and processing-time measurement. It must
not choose final DirectShow timestamps, apply queue-depth correction, decide
drop/repeat, or call downstream `Deliver()`.

The result must retain the source frame number, pipeline epoch, capture time,
nominal rate, discontinuity/scene/subtitle flags, source and output formats,
the sample, and processing duration.

### `ProcessedFrameQueue`

Own converted samples between processing and delivery, preserve the existing
converted-queue policy, wake the delivery worker, safely release samples on
flush/shutdown, discard stale epochs, and expose depth, wait, and overflow
statistics.

### `VideoTimingController`

Own clock/synthetic timestamp generation, smoothed frame duration,
rational-remainder accumulation, monotonic enforcement, fixed PPM, queue-depth
and display-phase correction, scene-aware drop/repeat planning, and timing
generation/reset state. It accepts value-type timing input and returns a
`TimingDecision` containing presentation start/stop, drop/repeat choice,
applied PPM, phase and queue errors, strategy, and timing generation.

It must not own queues, allocate or modify pixel buffers, perform subtitle or
scene analysis, call `IMediaSample::SetTime`, or call downstream delivery.

### Timing configuration

Replace the internal growth of monolithic timestamp modes with composable
dimensions while retaining existing names during migration:

- timestamp base: capture clock, smoothed capture clock, synthetic rational,
  or none;
- cadence correction: none, fixed PPM, queue-depth controller,
  display-phase controller, or scene-aware drop/repeat.

Initially map existing modes such as `CLOCK_CLOCK`, `CLOCK_RATIONAL`, and
`RATIONAL_RATIONAL` to equivalent combinations. A compatibility adapter may
retain the existing DirectShow mode type while equivalence is proven.

### `DirectShowFrameDeliverer`

Apply a timing decision to a processed sample, set sample times and flags,
apply required metadata, call downstream delivery, measure delivery duration,
and return structured results. It provides the DirectShow flush/new-segment
operations used by the coordinator, but must not independently initiate a
reset transaction. It must not calculate timestamps, estimate rates, change
PPM, inspect queue depth to make cadence decisions, or convert frames.

## Epoch and reset contract

Every frame and timing decision carries a common `PipelineEpoch`. Create a new
epoch for graph activation, flush, a capture discontinuity requiring restart,
renderer restart, media-type change, HDR/SDR graph reconfiguration, or an
explicit user reset. Workers discard work whose epoch differs from the current
epoch.

Reset coordination must be explicit: mark reset in progress; create the new
epoch; stop accepting old-epoch work; flush raw and processed queues; reset
processing and timing state; perform DirectShow `BeginFlush`/`EndFlush`/
`NewSegment` as required; then resume capture and worker processing.

Feature-specific generations may remain only when they represent a lifetime
different from the pipeline epoch. Capture timestamps must remain separate from
scheduled presentation times.

## Worker boundaries

The processing worker may pop a current-epoch raw frame, call `FrameProcessor`,
and push the result. The delivery worker may pop a current-epoch processed
frame, build timing input, call `VideoTimingController`, skip an explicitly
planned drop, or call `DirectShowFrameDeliverer` and feed back the result.
The coordinator may orchestrate these operations but detailed logic must stay
inside the owning component.

## Diagnostics and observability

Each delivered or intentionally dropped frame must be traceable, at least in
sampled high-frequency logs or aggregate/latest-state snapshots, by pipeline
epoch, source frame number, capture timestamp, scheduled start/stop, raw and
processed queue depth, timing strategy, PPM, phase error, queue error, planned
correction, scene-boundary status, processing duration, delivery duration, and
delivery result.

Continuously available counters must include captured frames, raw overflows,
processed frames and failures, processed overflows, stale-epoch discards,
timing decisions, planned drops/repeats, successful/failed deliveries,
pipeline resets, and renderer restarts.

## Migration plan

1. Characterize current behavior at 23.976 and 59.94 Hz and create golden
   traces for queues, frame counters, timestamps, PPM, scene corrections,
   resets, and delivery results.
2. Extract `FrameTimingInput` and `TimingDecision` into
   `VideoTimingController`; compare decisions against golden traces without
   changing algorithms.
3. Preserve and report capture and presentation timestamps independently.
4. Encapsulate raw and processed queues, including ownership, signaling,
   overflow, flush, and depth reporting.
5. Extract conversion and analysis into `FrameProcessor`.
6. Extract sample flags, timestamps, delivery, and delivery diagnostics into
   `DirectShowFrameDeliverer`.
7. Reduce the output pin to lifecycle, ownership, worker coordination, reset,
   data flow, and fatal-error handling.
8. Map existing timestamp modes to composable timing dimensions after
   equivalent behavior is demonstrated.

## Testability acceptance criteria

The timing controller must run in unit tests without a DirectShow graph,
renderer, capture device, worker thread, or real `IMediaSample`.

Tests must cover exact 60000/1001 and 24000/1001 timing, rational remainder
accumulation, monotonicity, positive/negative PPM, capture faster/slower than
display, queue growth/depletion, discontinuity, reset and epoch replacement,
scene-aware inputs, missing/invalid timestamps, missing display-rate
measurement, delivery failure, and long-running timestamp stability.

Replayable traces must compare old and new timing decisions. Simulations must
keep timestamp error bounded for at least four hours at 60000/1001 and
24000/1001.

## Functional and nonfunctional acceptance criteria

- Existing timestamp modes remain available and produce equivalent timestamps
  for identical recorded inputs, except for documented approved
  nondeterminism.
- Raw and processed queue behavior remains unchanged.
- Ownership is RAII-safe and leak-free through playback, reset, flush, and
  shutdown.
- No obsolete-epoch frame can be delivered.
- Capture timestamps remain available after presentation scheduling.
- Conversion/analysis does not calculate final presentation timestamps.
- Timing code does not call DirectShow delivery APIs, and delivery code does
  not calculate drift, PPM, or queue correction.
- Reset clears timing state, rational remainder, stale queues, and pending
  correction plans.
- Shutdown waits for workers before destroying queues/components.
- No raw-queue, processed-queue, state, or allocator lock is held during
  downstream delivery. A narrow delivery-serialization guard is permitted;
  `BeginFlush` must be issued before waiting for it.
- Capture callbacks remain nonblocking except for the minimum enqueue/reject
  work.
- No material latency increase, unnecessary frame copies, use-after-free,
  deadlock, or leaked samples is introduced.
- Existing pipeline statistics remain available or are replaced with
  equivalent structured metrics.
- Existing live playback is validated with the external renderer.
- Architecture and component interactions are documented.

## Out of scope

Replacing DirectShow or the external renderer; converting workers to a generic
thread pool; introducing lock-free queues; rewriting GPU conversion;
redesigning subtitle or scene detection; changing cadence policy; adding a
new PLL or drop/repeat behavior; removing timestamp modes; and changing user
configuration defaults.

Any intentional behavioral improvement must be a follow-up story after this
refactor is validated.

## Definition of done

The components are implemented with clear ownership, the output pin is mainly
a coordinator, timing runs independently in unit tests, golden traces and
long-duration simulations pass, reset/shutdown tests prove no stale delivery
or lifetime defects, capture/presentation times are independently observable,
queue/timing metrics remain available, and any behavior difference is
documented and explicitly approved.
