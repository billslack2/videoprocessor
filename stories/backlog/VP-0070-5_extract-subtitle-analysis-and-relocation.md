# VP-0070-5: Extract subtitle analysis and relocation pipeline

## Status

Backlog (2026-08-02). This task supersedes VP-0066-5 and keeps the substantial
subtitle content-processing extraction with the story that owns its behavior
and acceptance contract. It must not reopen or alter the accepted VP-0066 live
timing pipeline.

## Parent and dependencies

Parent: [VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md).

Dependencies: accepted VP-0070-1 through VP-0070-4 CueSet, rendering, and live
validation contracts, plus the stable VP-0066 epoch, processing, and lifecycle
seams. Supersedes [VP-0066-5](../will-not-do/VP-0066-5_extract-subtitle-analysis-and-relocation.md).

## Objective and scope

Extract subtitle acquisition, OCR/GPU/classical detection, cue tracking,
source restoration, and P010 compositing/relocation into explicit worker-owned
components behind the accepted VP-0070 CueSet contract. Preserve the current
latest-frame-only handoff, cancellation and shutdown behavior, generation
invalidation, P010 constraints, and scene-informed panel state.

The extraction must not introduce another frame queue, full-frame copy,
conversion-to-delivery worker transition, blocking detector wait, or timing
policy. `CBufferedLiveSourceVideoOutputPin` remains the owner/coordinator at
the stable VP-0066 seam and does not regain content-analysis decisions.

## Acceptance criteria

- Fake and fixture tests cover acquisition coalescing, cancellation,
  generation invalidation, shutdown, stale-result rejection, CueSet tracking,
  source restoration, and relocation decisions.
- A full P010 fixture proves output pixels and subtitle state are equivalent
  at the conversion boundary.
- SDR and HDR live runs in Alpha and DirectShow/madVR show no delivery,
  queue-depth, reset, or latency regression.
- The buffered pin is only the component owner and conversion-worker
  coordinator for this feature.
- VP-0066 Rational-Rational and Clock-Smart2 stable-baseline tests remain
  unchanged and passing.
