# VP-0066-1: Characterize the live output pipeline with replayable golden traces

## Status

In Progress. This task establishes the behavior-preserving baseline required
by the VP-0066 refactor. It does not change pipeline behavior or start
component extraction.

Readiness review (2026-07-30): the confirmed implementation base is
`origin/v1.1.015-beta` at `785e591` (`Merge VP-0065 Alpha retirement
transition fix`). Work proceeds in the clean source worktree
`C:\Users\bslac\vp\videoprocessor-vp0066-1` on
`codex/vp-0066-1-golden-traces`. The source checkout named in tracker guidance
is dirty and divergent and will not be modified. madVR remains the required
DirectShow downstream renderer; its configured internal queues are opaque, and
no madVR `IQualityControl` or queue-depth feedback may be used.

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
timestamp start/stop, PPM, phase and **VP-owned** queue error,
timing/correction choice, scene/discontinuity state, reset/renderer-restart
epoch, processing and delivery result. Record the exact madVR CPU/GPU queue
configuration as test metadata, not as a measured runtime value. Record
baseline latency for capture-to-conversion start, conversion duration,
converted-queue residence, capture-to-`Deliver()`, and end-to-end
capture-to-screen where externally measurable.

## Acceptance criteria

- Recorded 24000/1001 and 60000/1001 traces cover steady state, queue
  growth/depletion, positive and negative PPM, scene correction,
  discontinuity/reset, renderer handoff, and delivery failure.
- A deterministic test/tool replays the traces and reports the compared timing
  decisions and lifecycle events without requiring a capture device or live
  renderer.
- The baseline documents trace provenance, configuration, known unavoidable
  nondeterminism, comparison tolerances, and the configured madVR queue
  settings. It does not claim to know madVR queue occupancy.
- Before/after measurements demonstrate that instrumentation adds no queue,
  copy, handoff, or required buffering, and stays within the root story's
  one-millisecond equivalent-condition capture-to-delivery guardrail.

## Out of scope

Changing timing algorithms, queue policy, DirectShow delivery, configuration,
or component ownership. Those changes belong to the following child tasks.
