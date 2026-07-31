# VP-0066-4: Integrate DirectShow delivery and lifecycle coordination

## Status

In Progress (2026-07-30). `DirectShowFrameDeliverer` now owns the media-type
attachment/delivery/completion transaction for buffered and unbuffered pins,
plus the existing first-delivered-sample discontinuity and SMART late-bound
stop preparation. `DirectShowDeliveryOutcomeClassifier` now classifies the
existing latency bands and success/failure counter effects without a graph;
`DirectShowSegmentTransition` owns the renderer-facing
BeginFlush → state-transition → EndFlush → NewSegment order. Both have
controlled-callback tests and a live x64 Release reset validation with no
observed behavior change. Final completion remains dependent on VP-0066-3's
golden-trace/latency evidence.

## Parent and dependency

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

Dependency: [VP-0066-3](VP-0066-3_extract-epoch-aware-frame-transport-and-processing.md).
Completion evidence rolls up to VP-0066.

## Objective and scope

Extract `DirectShowFrameDeliverer` and reduce
`CBufferedLiveSourceVideoOutputPin` to DirectShow-facing lifecycle,
component ownership, worker coordination, epoch/reset orchestration,
high-level data flow, and fatal downstream-error handling. The deliverer
applies a supplied `TimingDecision`, sample flags/times and metadata, performs
flush/new-segment operations, calls downstream delivery, and returns measured
structured results. It does not calculate drift, PPM, queue correction, or
pixel conversion.

Integrate reset as an explicit epoch transition: reject old work, flush both
queues, reset processor/timing state, perform DirectShow flush/new-segment in
the required order, then resume workers. Preserve the two existing worker
boundaries and nonblocking capture callback.

## Acceptance criteria

- Deliverer tests with a controlled downstream fake prove timing/flags/metadata
  application, planned-drop handling, delivery success/failure reporting, and
  flush/new-segment order without running a live graph.
- Lifecycle tests prove reset, renderer restart, media-type/HDR-SDR
  reconfiguration, discontinuity, flush, and shutdown cannot deliver an
  obsolete-epoch frame, leak a sample, deadlock, or destroy components before
  workers stop.
- End-to-end replay of VP-0066-1 traces shows equivalent decisions, queue
  policy, delivery results, and structured diagnostics. Existing timestamp
  modes, renderer handoff, and pipeline statistics remain available or have
  equivalent structured replacements.
- Live external-renderer validation at 24000/1001 and 60000/1001 passes with
  no material latency change: capture-to-delivery remains within one
  millisecond under equivalent queue conditions and no queue, copy, worker
  handoff, preroll, or minimum buffering was added.
- Architecture/ownership documentation identifies the two worker boundaries,
  component contracts, epoch/reset order, and diagnostic fields.

## Out of scope

Changing cadence policy, replacing DirectShow or the renderer, adding a new
PLL or queue architecture, or changing configuration defaults. Intentional
behavior improvements require a follow-up root story.
