# VP-0184: Expose primary libplacebo tone-mapping and peak-detection controls

## Status

Backlog

Created 2026-09-12 at the user's request from batch 1 (audit ranks 1-7).
Implementation has not started.

## User story

As a VP Renderer operator tuning HDR for a calibrated projector, I want direct
control over scene brightness, spline contrast, peak adaptation, and local
contrast scale, so I can tune the picture without being limited to library presets.

## Scope and audited baseline

The 2026-09-08 audit used v1.3.005-beta at
89d55ca5791af7e357b213878fe2f75e126c84bc and bundled libplacebo 7.360.1.
These are historical baseline values, not a pinned implementation branch or
recommended changes to an operator's saved settings. Revalidate against the
current remote beta and bundled library before implementation.

| Priority | Proposed config key | libplacebo field | Audited inherited value | Purpose |
| --- | --- | --- | --- | --- |
| 1 | knee_adaptation | tone_constants.knee_adaptation | 0.4 | Scene-average brightness adaptation for spline/ST2094 methods |
| 2 | spline_contrast | tone_constants.spline_contrast | 0.5 | Midtone contrast versus shadow/highlight preservation |
| 3 | peak_smoothing_period | peak_detect_params.smoothing_period | 20 frames | Temporal stability versus adaptation speed |
| 4 | scene_threshold_low / scene_threshold_high | peak_detect_params.scene_threshold_low / scene_threshold_high | 1 / 3 PQ-percent units | Scene-change bypass of smoothing |
| 5 | peak_percentile | peak_detect_params.percentile | 99.995 HQ; 100 Standard | Sensitivity to isolated bright highlights |
| 6 | black_cutoff | peak_detect_params.black_cutoff | 1 PQ-percent unit | Near-black exclusion in peak/average analysis |
| 7 | contrast_smoothness | color_map_params.contrast_smoothness | 3.5 | Spatial scale of existing contrast recovery |

## Required behavior

- Expose all eight scalar fields in configuration, profile resolution, the
  native Config UI, renderer settings transport, and libplacebo projection.
  Use the existing renderer-profile ownership model; reconcile placement with
  VP-0174 instead of introducing a parallel settings namespace.
- Omitted values and explicit Auto preserve the selected quality/peak preset's
  current value. Resolve presets first, then apply explicit field overrides.
  Distinguish profile inheritance from resetting a field to library Auto.
- Keep peak detection Off when it is Off: a stored peak tuning value must not
  enable the detector. Preserve existing HQ/Standard/Auto selection semantics.
- Show the resolved numeric Auto value, units, and applicability. Present
  spline contrast only when spline is selected; gate knee adaptation on the
  supported algorithm. Explain that contrast smoothness has no effect when
  contrast recovery is zero and peak controls are inactive with detection Off.
  Preserve stored values when controls become inapplicable.
- Validate finite values against the bundled API/options contract. Baseline
  ranges are knee adaptation 0..1, spline contrast 0..1.5, smoothing 0..1000,
  scene thresholds/percentile/black cutoff 0..100, smoothness 1..32. Validate
  enabled scene thresholds as an ordered pair. Preserve documented special
  values: zero smoothing disables smoothing; either zero threshold disables
  scene-change logic; percentile 0 or 100 measures the brightest pixel; zero
  black cutoff disables its filtering. Show actionable validation feedback.
- Apply changes through the established configuration apply/lifecycle policy,
  with safe ownership of projected parameter structures. Classify peak-state
  reset requirements from the actual library behavior; avoid stale statistics
  or unnecessary renderer rebuilds. Do not introduce per-frame shader rebuilds.
- Update configuration schema/value references, help, and effective-value
  diagnostics. Explain that black cutoff is analysis filtering, not display
  black nits, and smoothing is measured in frames, not milliseconds.

## Acceptance criteria

1. Every field round-trips through file/UI/profile selection and arrives at
   the expected libplacebo field; explicit zero is not mistaken for Auto.
2. Regression tests prove that omitted/Auto controls preserve High, Balanced,
   and Fast behavior, including detection Off and existing explicit overrides.
3. Tests cover invalid/non-finite/boundary values, threshold relationships,
   switching algorithms, inherited overrides, and changing active profiles.
4. Deterministic HDR fixtures exercise scene cuts, fades, isolated highlights,
   and near-black/letterbox content. Record temporal and image comparisons at
   24 and 60 fps; confirm intended control effects and unchanged default output.
5. Contrast-smoothness validation includes recovery zero and nonzero. Preserve
   SDR code-value behavior, calibrated target luminance, display LUT ordering,
   gamut selection, output transfer/range, and dithering.
6. Relevant automated tests and an x64 Release build pass. Record UI validation
   and render-cost/transition evidence before review; deploy only if requested.

## Dependencies and next action

Coordinate with VP-0174's current calibrated-display UI and profile contract.
This story establishes the shared control/Auto/validation path reused by
VP-0185. Next: readiness review against current beta and the bundled API,
then implement this batch independently of the advanced controls.

## Out of scope

Batch 2 advanced constants (VP-0185), new algorithms, metadata policy,
visualizations, LUT precision, delayed peak detection, inverse tone mapping,
gamut expansion, and changes to existing defaults or deployed configuration.

## Evidence locations

- src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.cpp and .h
- src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp
- src/VideoProcessor-Lib/RendererProfileConfig.h
- src/VideoProcessor-Config/ConfigEditorWindow.cpp
- 3rdparty/libplacebo/include/libplacebo/tone_mapping.h
- 3rdparty/libplacebo/include/libplacebo/shaders/colorspace.h
- https://libplacebo.org/options/
