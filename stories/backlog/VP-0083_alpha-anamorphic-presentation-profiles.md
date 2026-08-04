# VP-0083: Alpha anamorphic presentation profiles

## Status

Backlog design and bounded feasibility spike. No implementation branch has
been chosen.

## User story

As an Alpha-renderer user with an anamorphic projector/lens arrangement, I
want to select an anamorphic presentation treatment together with my viewport
profile, so VP applies the intended geometric stretch through its normal
renderer scaling path without changing source detection, HDR processing, or
my normal CIH/scope configuration.

## Background

The Alpha renderer already owns the final libplacebo render target and scales
the processed frame into its destination viewport. An anamorphic correction is
therefore expected to be a geometric presentation transform (normally a linear
horizontal or vertical scale), not a new capture conversion, a crop detector,
or a shader requirement. libplacebo's render path supports selecting a
destination rectangle/size; the spike must verify the exact API and placement
in VP's current renderer before claiming a shipped capability.

The correction must be configured where the screen/viewport is selected. It
is a property of the physical presentation profile, not a per-movie NLS rule
and not a global capture setting.

## Configuration design to validate

Keep the setting in the existing viewport profile group, for example:

```ini
[profiles.viewport.scope]
when: $key=="F2"
screen_aspect: 2.35:1
anamorphic_input_aspect: 5:4
anamorphic_output_aspect: 4:3
```

The proposed explicit pair avoids an ambiguous word such as `stretch`:

- `anamorphic_input_aspect` is the aspect of the image before VP's correction.
- `anamorphic_output_aspect` is the intended aspect after correction.
- VP derives the linear factor from the two values and reports both aspects and
  the resulting scale in the OSD/log.
- Omitting both settings means no anamorphic transform.
- Supplying only one, an invalid ratio, or an unsupported transform must be a
  validated configuration error with a clear log message and safe no-transform
  fallback.

The spike must confirm whether these names and the input/output direction match
real-world lens use. It may replace them with one less error-prone canonical
form only if the documentation gives an equally unambiguous worked example.
Do not expose a raw unsigned scale factor as the only user-facing option.

## Scope

1. Create a short, non-production feasibility spike against the current Alpha
   renderer/libplacebo integration. Identify the exact final-render scaling
   stage and prove that a linear anamorphic transform can be applied there
   without an intermediate CPU copy, extra presentation frame, or loss of HDR
   metadata/color-management behavior.
2. Define the renderer contract and coordinate system: whether the transform
   is pre- or post-screen-fit, how it interacts with physical lens expansion,
   and how a profile's `screen_aspect` participates in the final destination
   rectangle.
3. Add typed anamorphic settings to the existing viewport profile parser and
   runtime profile state. F2/F3 or any configured viewport hotkey must select
   the matching anamorphic treatment atomically with the viewport.
4. Apply the transform only in the Alpha renderer's final presentation path.
   Preserve source raster dimensions, active-picture/black-bar analysis, NLS
   input geometry, subtitle analysis, capture timing, HDR input metadata, and
   SDR/HDR output treatment.
5. Decide and document pipeline order. The expected default is: analyze source
   geometry -> NLS/crop decisions -> color/tone processing -> anamorphic
   presentation scale -> final screen fit/present. The spike must prove or
   deliberately adjust this order.
6. Extend the Alpha OSD with a compact `Anamorphic` line only when enabled,
   showing the configured input/output aspects and calculated scale. Log
   profile selection, validation result, applied transform, output rectangle,
   and any safe fallback; do not log every rendered frame.
7. Document each setting, accepted ratio syntax, AUTO/omitted behavior if
   supported, directionality, and a worked `5:4 -> 4:3` example in the
   canonical configuration help.

## Non-goals

- Do not modify madVR behavior, create a madVR shader, or claim renderer
  parity for a renderer that VP does not own.
- Do not use NLS as the implementation mechanism. NLS is nonlinear,
  content-aware stretch; anamorphic correction is a deterministic linear
  presentation transform.
- Do not infer lens ratio from movie content or automatically change it during
  mixed-aspect scenes.
- Do not alter capture sample format, force P010 conversion, or add a GPU/CPU
  readback path solely for this feature.
- Do not change the current profile when a configuration value is malformed;
  retain the previous safe/no-anamorphic output and report the reason.

## Acceptance criteria

- A viewport profile can enable a validated, documented anamorphic transform
  for the Alpha renderer; a profile without it is visually and behaviorally
  unchanged.
- The `5:4 -> 4:3` configuration has an unambiguous, tested visual result and
  a clearly documented transform direction.
- Switching viewport profiles changes both screen fit and anamorphic state
  together without a stale transform, extra visible frame, or renderer restart
  unless libplacebo resource constraints demonstrably require one.
- NLS active-rectangle detection remains based on the original captured image,
  not the anamorphically transformed presentation.
- SDR, HDR input tone mapping, SDR BT.2020 output profiles, scope/normal
  viewports, fullscreen/windowed presentation, and OSD placement are validated
  with and without the transform.
- The x64 Release build and focused configuration/geometry tests pass.

## Dependencies and related stories

- VP-0038: Generic viewport state and screen-aware NLS configuration.
- VP-0044: Alpha OSD visible-picture anchoring and scaling.
- VP-0045: Canonical vpvr configuration namespace.
- VP-0052: Alpha runtime layout and `vprenderer` directory.
- VP-0019: SDR BT.2020 display profile/hotkey behavior.

