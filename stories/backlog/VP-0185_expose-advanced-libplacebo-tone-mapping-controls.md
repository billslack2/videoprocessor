# VP-0185: Expose advanced libplacebo tone-curve, gamut, and metadata controls

## Status

Backlog

Created 2026-09-12 at the user's request from batch 2 (audit ranks 8-14).
Implementation has not started. Planned after VP-0184.

## User story

As an advanced VP Renderer operator, I want algorithm-specific curve and gamut
controls plus an explicit metadata policy, so I can refine HDR rendering and
diagnose problematic sources while retaining reproducible preset defaults.

## Scope and audited baseline

The 2026-09-08 audit used v1.3.005-beta at
89d55ca5791af7e357b213878fe2f75e126c84bc and bundled libplacebo 7.360.1.
Revalidate these inherited values and applicability against current beta before
implementation; they do not prescribe new defaults or a source integration base.

| Priority | Proposed config keys | libplacebo destination | Audited inherited values |
| --- | --- | --- | --- |
| 8 | knee_minimum, knee_maximum, knee_default | tone_constants, matching fields | 0.1, 0.8, 0.4 |
| 9 | knee_offset | tone_constants.knee_offset | 1.0; BT.2390 specification value is 0.5 |
| 10 | perceptual_deadzone, perceptual_strength | gamut_constants, matching fields | 0.30, 0.80 |
| 11 | softclip_knee, softclip_desat, colorimetric_gamma | gamut_constants, matching fields | 0.70, 0.35, 1.80 |
| 12 | tone_map_metadata | color_map_params.metadata | PL_HDR_METADATA_ANY |
| 13 | slope_tuning, slope_offset | tone_constants, matching fields | 1.5, 0.2 |
| 14 | reinhard_contrast | tone_constants.reinhard_contrast | 0.5 |

## Required behavior

- Extend VP-0184's profile resolution, schema, UI, settings transport,
  projection, Auto display, and diagnostics conventions for all thirteen
  fields above. Apply explicit overrides after preset resolution. Missing/Auto
  values preserve current behavior, including the BT.2390 offset of 1.0.
- Group these as Advanced controls with algorithm-specific applicability.
  Knee bounds/default apply to the ST2094-style knee calculation; knee offset
  applies to BT.2390; slope controls to spline; Reinhard contrast to Reinhard.
  Perceptual controls apply to perceptual gamut mapping; softclip knee applies
  to perceptual/softclip; softclip desaturation applies only to softclip.
  Verify actual colorimetric-gamma consumers in the bundled source before
  declaring UI applicability. Retain inactive values through algorithm changes.
- Explain knee_default as the fallback without source scene-average metadata,
  not a replacement for an available dynamic average. Explain the BT.2390
  specification/library-default distinction without silently changing it.
- Validate finite values against the actual bundled API/options contract and
  any normalization it performs. Historical ranges: knee minimum (0,0.5),
  maximum (0.5,1), default within the resolved minimum/maximum; knee offset
  0.5..2; perceptual and softclip constants 0..1; colorimetric gamma 0..10;
  slope tuning 0..10; slope offset 0..1; Reinhard contrast (0,1).
  Validate the resolved knee tuple after combining Auto and explicit values.
  Reject inconsistent tuples with clear feedback rather than silent clamping.
- Expose metadata choices Auto/Any, None, HDR10, HDR10+, and CIE Y with precise
  enum mapping. Auto retains preset policy; explicitly selected Any uses the
  library's automatic preference. Document what each choice actually does,
  including missing metadata and fallback behavior. Do not describe None as
  disabling tone mapping or claim that selecting HDR10+ creates dynamic metadata.
- Establish how metadata policy interacts with measured peak/average values,
  HDR10 source data, and VP's synthesized LLDV metadata. Preserve detection
  enablement and existing LLDV synthesis/profile settings. If library behavior
  differs from a UI label's implication, correct the label/help before release.
- Follow the established apply/lifetime policy; expose effective values and
  applicability without altering target calibration or introducing unnecessary
  frame-path resource creation. Update public configuration and in-app help.

## Acceptance criteria

1. All thirteen fields round-trip through configuration/UI/profiles and project
   to the correct libplacebo fields/enums. Missing/Auto parity holds for every
   quality preset and existing algorithm choice.
2. Tests cover valid limits, non-finite/invalid values, mixed Auto/explicit knee
   tuples, inheritance, resetting overrides, and switching algorithms/profiles.
3. Deterministic curve tests demonstrate knee bounds/fallback, spline slope,
   BT.2390 offset 0.5 versus 1.0, and Reinhard contrast. Only applicable controls
   affect each algorithm; unchanged defaults preserve existing results.
4. Wide-gamut fixtures demonstrate gamut-constant effects without changing
   target primaries, tone-map destination, output transfer/range, or calibration
   LUT placement. Preserve SDR behavior and output dithering.
5. Metadata tests cover each policy with complete, missing, and conflicting
   inputs, detector On/Off, and synthesized LLDV input. Record actual fallback
   and precedence behavior; selected policies must not silently become another
   enum or be advertised as available source metadata.
6. Relevant automated tests and an x64 Release build pass. Record UI, image,
   transition, and performance evidence before review; deploy only if requested.

## Dependencies and next action

Implement after VP-0184 is accepted, reusing its settings/control infrastructure.
Coordinate with VP-0174's current profile and calibrated-display UI contract.
Next: readiness review of current beta, exact algorithm consumers, and metadata
precedence in the bundled source before implementing the advanced controls.

## Out of scope

New tone/gamut algorithms, legacy removed controls, LUT visualization or
precision, delayed peak detection, inverse tone mapping, gamut expansion,
changes to source metadata synthesis, and changed defaults/deployed settings.

## Evidence locations

- VP-0184_expose-primary-libplacebo-tone-mapping-controls.md (same state folder initially)
- src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.cpp and .h
- src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp
- src/VideoProcessor-Lib/RendererProfileConfig.h
- src/VideoProcessor-Config/ConfigEditorWindow.cpp
- 3rdparty/libplacebo/include/libplacebo/tone_mapping.h
- 3rdparty/libplacebo/include/libplacebo/gamut_mapping.h
- 3rdparty/libplacebo/include/libplacebo/shaders/colorspace.h
- https://libplacebo.org/options/
