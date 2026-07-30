# VP-0066-1: Characterize the live output pipeline with replayable golden traces

## Status

Backlog. This task establishes the behavior-preserving baseline required by
the VP-0066 refactor. It does not change pipeline behavior or start component
extraction.

## Parent and dependency

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

This is the first child task and has no child-task dependency. VP-0066-2 may
begin only after its evidence is accepted.

## Objective and scope

Create reproducible, versioned trace fixtures and a replay/comparison harness
for the existing live output pipeline at exact 24000/1001 and 60000/1001
rates. Instrument without adding a queue, worker handoff, frame copy, startup
preroll, or minimum-buffering requirement.

The traces must record enough input and output to compare timing and lifecycle
behavior: source frame number, capture time, raw and converted queue depth,
timestamp start/stop, PPM, phase and queue error, timing/correction choice,
scene/discontinuity state, reset/renderer-restart epoch, processing and
delivery result. Record baseline latency for capture-to-conversion start,
conversion duration, converted-queue residence, capture-to-`Deliver()`, and
end-to-end capture-to-screen where it is measurable.

## Acceptance criteria

- Recorded 24000/1001 and 60000/1001 traces cover steady state, queue
  growth/depletion, positive and negative PPM, scene correction,
  discontinuity/reset, renderer handoff, and delivery failure.
- A deterministic test/tool replays the traces and reports the compared timing
  decisions and lifecycle events without requiring a capture device or live
  renderer.
- The baseline documents trace provenance, configuration, known unavoidable
  nondeterminism, and comparison tolerances.
- Before/after measurements demonstrate that instrumentation adds no queue,
  copy, handoff, or required buffering, and stays within the root story's
  one-millisecond equivalent-condition capture-to-delivery guardrail.

## Out of scope

Changing timing algorithms, queue policy, DirectShow delivery, configuration,
or component ownership. Those changes belong to the following child tasks.
