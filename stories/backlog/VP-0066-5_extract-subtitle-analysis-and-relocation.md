# VP-0066-5: Extract subtitle analysis and relocation pipeline

## Status

Backlog. This is a follow-up to the core VP-0066 transport/processing work.
It is deliberately separate because subtitle OCR, tracking, and P010
relocation are substantial content-processing features, not prerequisites for
the live capture → DirectShow → madVR timing/readiness change.

## Parent and dependency

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

Dependency: accepted VP-0066-3 transport and processing core.

## Objective and scope

Extract the subtitle acquisition worker, OCR/GPU/classical detection, tracking,
and P010 compositing/relocation into explicit worker-owned components. Preserve
the current latest-frame-only handoff, cancellation and shutdown behavior,
generation invalidation, P010-only constraints, and scene-informed panel state.

No additional frame queue, full-frame copy, delivery-thread work, or live
latency requirement may be introduced.

## Acceptance criteria

- Fake/fixture tests cover acquisition coalescing, cancellation, generation
  invalidation, shutdown, stale-result rejection, and relocation decisions.
- A full P010 fixture proves output pixels and subtitle state are equivalent at
  the conversion boundary.
- SDR and HDR live runs with subtitle repositioning enabled show no delivery,
  queue-depth, or reset regression.
- The buffered pin becomes only the component owner and conversion-worker
  coordinator for this feature.

## Out of scope

Changing subtitle detection quality, OCR models, relocation policy, or user
configuration defaults.
