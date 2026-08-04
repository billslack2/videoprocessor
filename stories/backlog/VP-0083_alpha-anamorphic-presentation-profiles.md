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

## Design review — configuration and renderer contract (2026-08-04)

The review concludes that this is a small Alpha presentation-geometry feature,
but it needs a typed viewport contract rather than a loose libplacebo setting.
libplacebo has no renderer parameter named "anamorphic ratio" that VP should
blindly pass through. VP already owns the final `pl_frame::crop` rectangle. The
implementation must derive the corrected image aspect and fit that aspect into
the selected physical screen rectangle before calling `pl_render_image`.

### Canonical viewport configuration

An anamorphic treatment is optional and belongs only in a
`[profiles.viewport.<name>]` section. Both settings are required together:

```ini
[profiles.viewport.normal]
when: $key=="F3"
screen_aspect: 16:9

[profiles.viewport.scope_anamorphic]
when: $key=="F2"
screen_aspect: 2.35:1
anamorphic_input_aspect: 5:4
anamorphic_output_aspect: 4:3
```

`anamorphic_input_aspect` and `anamorphic_output_aspect` use the same shared,
strict ratio syntax as `screen_aspect`: positive decimal or `W:H` values,
including whitespace around the separator. A pair describes a *linear
horizontal presentation correction*, not a content-selection rule:

```text
horizontal_scale = anamorphic_output_aspect / anamorphic_input_aspect
```

The example therefore produces `1.333333 / 1.25 = 1.066667`: every rendered
image is 6.667% wider at the final presentation stage. `input` is the named
pre-correction reference and `output` is the named desired reference; VP does
not require each incoming movie or detected active picture to equal the input
ratio. That would make the lens correction incorrectly turn on and off during
mixed-aspect content.

Omitting both settings resolves to `AnamorphicMode::Off` and a scale of `1.0`.
Supplying exactly one setting, a malformed/non-finite/non-positive ratio, or a
derived scale outside the deliberately supported `0.5..2.0` range is a strict
profile validation error. A rejected profile must not change the selected
viewport; if a previously valid profile is already active, its current
no-transform or valid-transform presentation remains in effect. There is no
`AUTO` mode in the first increment, no global `[display]` setting, no raw
`anamorphic_scale` setting, and no per-shader or per-movie override.

The resolved immutable viewport snapshot must contain the profile identity,
screen aspect, subtitle settings, anamorphic enabled state, input/output
ratios, horizontal scale, and selection generation. The application publishes
one new snapshot on an F2/F3 (or equivalent) selection; the Alpha renderer
consumes that complete snapshot at a render-frame boundary. A renderer change
must restore the selected snapshot before its first presentation frame. The
old Boolean `SetScreenProfile(scopeScreen, ...)` boundary is insufficient for
this feature and must be replaced or adapted behind an aspect/settings-driven
viewport-apply API.

### Geometry order and exact Alpha implementation

The order is fixed so that anamorphic correction cannot affect source analysis:

```text
captured raster / active-picture analysis / trusted source crop / NLS
    -> libplacebo color and tone processing
    -> linear anamorphic presentation aspect
    -> fit inside selected physical screen_aspect viewport
    -> swapchain presentation
```

More precisely, in Alpha's existing final `configureScreenProfile`-equivalent
path:

1. Start with the full swapchain target rectangle.
2. Shrink it to the selected `screen_aspect`; this is the physical presentation
   viewport and remains the outer bound.
3. Preserve the normal resolved `source.crop`, including any independently
   authorized active-picture crop and subtitle displacement.
4. Calculate `presentation_aspect = aspect(source.crop) * horizontal_scale`.
5. Shrink the inner target rectangle to `presentation_aspect`, contained within
   the screen viewport. In the current libplacebo API this is the existing
   `pl_rect2df_aspect_set(..., panscan = 0)` / aspect-fit operation, using the
   corrected aspect in place of the unmodified source aspect.
6. Render once through the ordinary `pl_render_image` call and present once.

This is GPU-only rectangle math. It requires neither an intermediate texture
nor a CPU copy/readback, and it leaves the source `pl_frame`, source crop,
capture timing, input HDR metadata, color mapping, output color contract, and
display LUT behavior unchanged. It is intentionally *not* implemented by NLS:
NLS remains upstream, nonlinear, and driven by active content; the anamorphic
factor is a uniform final presentation scale.

Subtitle fitting and OSD anchoring must use the final corrected inner output
rectangle, while subtitle detection continues to inspect the uncorrected source
coordinates. The profile switch is render-boundary atomic: no frame may use
the new screen viewport with the old anamorphic scale (or vice versa). A
switch changes only rectangles and should not require a renderer restart or
shader-cache flush; if a platform limitation disproves that, it must be logged
and justified before adding a restart path.

### Renderer behavior and observability

Alpha is the only renderer that applies the transform. The profile parser may
accept the pair regardless of the selected renderer so that profile selection
remains stable, but a non-Alpha renderer must explicitly report
`Anamorphic: unavailable for this renderer; not applied` and must never pass
the setting to madVR or emulate it by changing a DirectShow media type. Alpha
reports the selected profile once per change, for example:

```text
Viewport: scope_anamorphic (screen 2.35:1)
Anamorphic: 5:4 -> 4:3, horizontal scale 1.066667
```

The OSD adds the `Anamorphic` line only while Alpha has an enabled, accepted
transform. State-change logs record profile/generation, both normalized ratios,
scale, source aspect, final inner rectangle, outer screen rectangle, and an
explicit safe-fallback reason. They must not emit per-frame geometry logs.

### Required proof and tests

1. Unit-test profile parsing for omitted pair, complete valid pair, only-input,
   only-output, whitespace/decimal forms, invalid/zero/non-finite values, and
   out-of-range derived scales.
2. Unit-test the geometry helper with normal and Scope screens, 4:3, 16:9,
   1.90, and 2.35 sources, using scale `1.0`, `16/15`, and a squeeze factor.
   Prove the output is contained by the screen viewport and that a `16/15`
   factor changes only the presentation aspect.
3. Add a deterministic `5:4 -> 4:3` reference-frame assertion: equal-height
   source geometry becomes exactly 16/15 wider, with no source-crop mutation.
4. Exercise profile changes while Alpha is rendering. Assert one coherent
   viewport generation per selection, no stale-factor frame, no restart, no
   queue discontinuity, and a no-op for a repeated same-profile selection.
5. Verify NLS/active-picture analysis receives identical source geometry with
   anamorphic Off and On; verify subtitle detection remains source-based while
   final subtitle/OSD placement follows the corrected target rectangle.
6. Cover SDR, HDR, SDR BT.2020/LUT output profiles, fullscreen/windowed, and
   normal/scope viewports. Complete a clean x64 Release build and targeted
   live projector/lens validation before deployment.

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

