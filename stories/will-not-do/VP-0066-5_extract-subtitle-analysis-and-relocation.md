# VP-0066-5: Extract subtitle analysis and relocation pipeline

## Status

Will Not Do (2026-08-02). This ID is retained for lineage but is superseded by
[VP-0070-5](../blocked/VP-0070-5_extract-subtitle-analysis-and-relocation.md),
where subtitle extraction belongs under the existing CIH subtitle story.
Subtitle OCR, tracking, and P010 relocation are substantial content-processing
features, not prerequisites for the completed VP-0066 live timing pipeline.

## Parent and dependency

Former parent: [VP-0066](../done/VP-0066_rearchitect-live-video-output-pipeline.md).

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
