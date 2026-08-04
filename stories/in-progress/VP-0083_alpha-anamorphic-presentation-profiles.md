# VP-0083: Alpha anamorphic presentation profiles

## Status

In Progress. Implementation and hardware validation were started on 2026-08-04
from `v1.1.015-beta` on `codex/vp-0083-anamorphic`.

## Implementation checkpoint (2026-08-04)

- Source commit `681baa1bde90af09004152b153b812f599a33172` adds the typed,
  viewport-owned `anamorphic_scale` field (decimal or ratio, range `0.5..2.0`)
  and publishes it with the resolved viewport snapshot.
- Alpha applies the scale only while fitting the final libplacebo destination
  rectangle; source crop authority, active-picture/NLS analysis, and color/HDR
  processing remain unchanged.
- The checked-in and deployed profiles select Scope plus `16:15` scale on
  `F8`; the existing `F2` selects the ordinary Scope profile without the
  scale.
- A clean x64 Release build succeeded and all 570 native tests passed.
- Deployment replaced the matched `VideoProcessor.exe` and
  `vprenderer\VideoProcessorVPRenderer.dll` pair. The live config was backed
  up at `C:\Videoprocessor\vp\backup-before-vp0083-20260804-103630` before
  the minimal hotkey/profile edit.

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
anamorphic_scale: 16:15
```

The single scale is independent of the physical screen aspect:

- `anamorphic_scale` is the final linear horizontal multiplier. A value greater
  than `1` widens the image and a value below `1` squeezes it. Vertical
  anamorphic scaling is deliberately unsupported by this feature.
- It accepts the same decimal or `W:H` ratio syntax as other ratios. `16:15`
  means `1.066667`, i.e. a 6.667% horizontal expansion.
- Omitting it means no anamorphic transform (`1:1`).
- An invalid or unsupported scale is a validated configuration error with a
  clear log message and safe no-transform fallback.

`screen_aspect: 2.35:1` remains the outer physical viewport; it does not tell
VP the lens multiplier. Inferring a multiplier from it would wrongly treat a
16:9 source on a 2.35 screen as needing a 2.35/1.777... horizontal stretch.
The one explicit lens-scale value is the same independent piece of information
madVR needs.

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
anamorphic_scale: 16:15
```

`anamorphic_scale` uses the same shared, strict ratio syntax as `screen_aspect`:
a positive decimal or `W:H` value, including whitespace around the separator.
It describes a *linear horizontal presentation correction*, not a
content-selection rule:

```text
horizontal_scale = anamorphic_scale
```

The example produces `16 / 15 = 1.066667`: every rendered image is 6.667%
wider at the final presentation stage. VP does not require an incoming movie
or detected active picture to match a nominated input ratio; that would make
the lens correction incorrectly turn on and off during mixed-aspect content.

Omitting the setting resolves to `AnamorphicMode::Off` and a scale of `1.0`.
A malformed/non-finite/non-positive ratio or a scale outside the deliberately
supported `0.5..2.0` range is a strict profile validation error. A rejected
profile must not change the selected viewport; if a previously valid profile is
already active, its current no-transform or valid-transform presentation
remains in effect. There is no `AUTO` mode in the first increment, no global
`[display]` setting, and no per-shader or per-movie override.

The resolved immutable viewport snapshot must contain the profile identity,
screen aspect, subtitle settings, anamorphic enabled state, horizontal scale,
and selection generation. The application publishes one new snapshot on an
F2/F3 (or equivalent) selection; the Alpha renderer consumes that complete
snapshot at a render-frame boundary. A renderer change must restore the
selected snapshot before its first presentation frame. The old Boolean
`SetScreenProfile(scopeScreen, ...)` boundary is insufficient for this feature
and must be replaced or adapted behind an aspect/settings-driven viewport-apply
API.

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
accept the scale regardless of the selected renderer so that profile selection
remains stable, but a non-Alpha renderer must explicitly report
`Anamorphic: unavailable for this renderer; not applied` and must never pass
the setting to madVR or emulate it by changing a DirectShow media type. Alpha
reports the selected profile once per change, for example:

```text
Viewport: scope_anamorphic (screen 2.35:1)
Anamorphic: horizontal scale 16:15 (1.066667)
```

The OSD adds the `Anamorphic` line only while Alpha has an enabled, accepted
transform. State-change logs record profile/generation, normalized scale,
source aspect, final inner rectangle, outer screen rectangle, and an explicit
safe-fallback reason. They must not emit per-frame geometry logs.

### Required proof and tests

1. Unit-test profile parsing for an omitted setting, valid scales,
   whitespace/decimal forms, invalid/zero/non-finite values, and out-of-range
   scales.
2. Unit-test the geometry helper with normal and Scope screens, 4:3, 16:9,
   1.90, and 2.35 sources, using scale `1.0`, `16/15`, and a squeeze factor.
   Prove the output is contained by the screen viewport and that a `16/15`
   factor changes only the presentation aspect.
3. Add a deterministic `16:15` reference-frame assertion: equal-height source
   geometry becomes exactly 16/15 wider, with no source-crop mutation.
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
   showing the configured horizontal scale. Log profile selection, validation
   result, applied transform, output rectangle, and any safe fallback; do not
   log every rendered frame.
7. Document each setting, accepted ratio syntax, AUTO/omitted behavior if
   supported, directionality, and a worked `16:15` example in the
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
- The `anamorphic_scale: 16:15` configuration has an unambiguous, tested visual
  result and a clearly documented transform direction.
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

