# VP-0099: Derive NLS geometry dynamically from panel, screen, and source envelope

## Status

Backlog. Created 2026-08-07 from developer direction while deploying VP-0098.
The developer's required principle is that VP knows the full output-panel size
and configured physical-screen ratio, so it must calculate any panel-to-screen
compensation and NLS shader geometry dynamically rather than rely on fixed
16:9/scope values.

This story is deliberately separate from VP-0089. VP-0099 corrects and proves
the coordinate and parameter contract for current NLS modes. VP-0089 may later
use that contract for its optional two-axis balanced transform.

## User story

As a CIH user whose configured screen ratio may differ from the projector or
display panel ratio, I want VP to derive the physical screen rectangle and NLS
mapping from runtime geometry, so changing panel resolution, screen ratio, or
trusted source envelope produces the correct shader parameters automatically
without screen-specific constants or manual retuning.

## Current behavior and unknown

VP already has most required facts:

- output panel width and height from the active swapchain/target;
- configured physical `screen_aspect`;
- the generation-current selected source presentation envelope from VP-0098;
- anamorphic destination scale and NLS policy settings; and
- dynamic Alpha hook parameters including `stretch_ratio` and `warp_axis`.

Current Alpha mapping derives `stretch_ratio` primarily from source and target
aspects, while the renderer separately constrains `target.crop` to the selected
screen rectangle. The shipped shader does not receive an explicit panel size,
screen rectangle, or normalized screen transform.

The key implementation unknown is where libplacebo evaluates `HOOKED_pos` for
this hook:

- If hook coordinates are already local to the selected source mapped into
  `target.crop`, panel-to-screen compensation belongs entirely in the target
  rectangle. Passing another compensation factor to the shader would
  double-correct the image.
- If any mapping is evaluated in full-panel output coordinates, the shader
  contract must receive a normalized screen offset/scale and perform its warp
  in screen-local coordinates.

Resolve this with a deterministic rendered-coordinate probe and libplacebo
contract evidence before changing production shader math. Do not infer the
answer from a visually plausible frame.

## Geometry contract

Let:

```text
panel size:             Wp x Hp
panel aspect:           P = Wp / Hp
physical screen aspect: S
source envelope aspect: A
anamorphic scale:       M
effective source aspect Ae = A * M (only when that profile is active)
```

For a centered screen without explicit insets, derive the physical screen
rectangle by one aspect-preserving fit of `S` inside the panel:

```text
if S > P:
    screen_width  = Wp
    screen_height = Wp / S
    screen_left   = 0
    screen_top    = (Hp - screen_height) / 2
elif S < P:
    screen_height = Hp
    screen_width  = Hp * S
    screen_top    = 0
    screen_left   = (Wp - screen_width) / 2
else:
    screen = full panel
```

The remaining edges are derived symmetrically. Fractional coordinates must use
one documented pixel-center/alignment policy rather than independent rounding
that changes aspect or introduces a one-pixel offset.

For current single-axis full-fill NLS, derive the required aspect correction
from the selected effective source and physical screen:

```text
horizontal candidate when Ae < S: S / Ae
vertical candidate when Ae > S:   Ae / S
passthrough when Ae == S within the configured tolerance
```

Safety policy may reject or cap a candidate, but it must not substitute a
hard-coded 16:9, 2.35, 2.40, or profile-name assumption. Partial-strength NLS
must interpolate from identity toward the required correction without
overshoot.

If shader coordinates require panel compensation, normalize through the
derived screen rectangle exactly once:

```text
screen_u = (panel_x - screen_left) / screen_width
screen_v = (panel_y - screen_top) / screen_height
```

Warp in screen-local coordinates, clamp to the selected source envelope, then
map back once. If `target.crop` already supplies this transform, record that
fact and do not add equivalent shader parameters.

## Scope

1. Extract a deterministic NLS geometry plan containing panel rectangle,
   physical screen rectangle, selected source envelope, effective source
   aspect, target aspect, correction ratio, warp axis, passthrough/safe-fit
   decision, and coordinate-space ownership.
2. Prove the libplacebo hook coordinate contract with a small diagnostic shader
   or render fixture across both letterboxed and pillarboxed screen rectangles.
3. Make the Alpha NLS hook consume only the dynamic parameters actually needed
   after that proof. Do not pass panel compensation merely because the values
   are available.
4. Audit the DirectShow/madVR NLS plan against the same source/screen math.
   Preserve backend boundaries: VP may derive its own HLSL parameters, but it
   must not configure madVR's independent physical-screen settings.
5. Recalculate dynamically on panel resolution, swapchain, viewport/profile,
   screen ratio, source envelope, anamorphic scale, renderer generation, and
   NLS policy changes. Ordinary frame-to-frame stability must not recompile the
   Alpha hook, restart the renderer, or churn the shader cache.
6. Use one final source envelope from VP-0098. NLS must not reacquire crop
   authority, inspect encoded bars, or alter presentation-envelope ownership.
7. Publish diagnostics for panel, screen rectangle, source envelope, effective
   source/target aspects, correction ratio, axis, strength/cap, coordinate
   transform owner, and final picture rectangle.

## Compatibility and impact review

- **No double compensation:** the largest risk is applying both a reduced
  target rectangle and a shader panel-to-screen transform for the same ratio.
- **NLS off and fallback:** ordinary linear fit must remain identical to
  VP-0098 and must not require shader parameters.
- **Anamorphic profiles:** establish explicitly whether `anamorphic_scale`
  participates before NLS ratio selection or only after an NLS bypass. Never
  apply the same optical correction twice.
- **Normal/full-panel use:** a screen matching the panel must reduce to the
  existing identity screen transform.
- **Dynamic envelopes:** bounded subtitle/UI expansion may change `A` and thus
  the requested NLS correction. Changes must be generation-current, bounded,
  and free of a crop/NLS split frame.
- **madVR:** VP-derived geometry must not imply that VP knows or changes
  madVR's internal zoom, masking, or screen-profile state.
- **VP-0089:** balanced two-axis distribution must consume this geometry plan
  rather than duplicate panel/screen/source arithmetic.

## Verification matrix

Test panel sizes at minimum:

- 1920x1080 and 3840x2160 (16:9);
- 4096x2160 (DCI-like); and
- at least one deliberately nonstandard synthetic panel aspect.

For each panel, test physical screens 4:3, 16:9, 1.85, 2.0, 32:15, 2.35, 2.40,
and 2.53, with representative 4:3 through 2.40 source envelopes. Prove:

- screen rectangle is centered, contained in the panel, and has aspect `S`;
- changing panel resolution without changing panel/screen/source aspects
  changes only pixel coordinates, not normalized NLS strength;
- changing `S` recalculates the screen rectangle and correction dynamically;
- changing `A` recalculates only the required mapping and never crop authority;
- NLS active, passthrough, safe-fit, unavailable, and off modes select the same
  final screen rectangle;
- no result applies compensation twice or leaves bars on both axes unless
  explicit insets require it;
- hook coordinates are continuous, monotonic, finite, and sample only inside
  the selected source envelope; and
- profile, source, renderer, and swapchain generation changes cannot reuse
  stale panel or screen geometry.

Live validation must cover the deployed 32:15 screen on its actual panel plus
at least one other screen ratio. Compare NLS on/off during subtitles, menus,
mixed-aspect transitions, pans, and anamorphic profile changes.

## Acceptance criteria

- Panel size, physical screen ratio, selected source envelope, and declared
  NLS policy are sufficient runtime inputs; no screen-shape-specific shader
  constant or `scope`/`normal` name is required.
- VP derives one physical screen rectangle and uses it consistently for linear
  fit, NLS, OSD, and diagnostics.
- The shader receives panel/screen compensation only if the proven hook
  coordinate contract requires it, and exactly one layer owns that transform.
- Runtime `stretch_ratio`, axis, and any screen-local transform update when
  their owning geometry changes without shader recompilation or renderer
  restart.
- NLS off remains the VP-0098 linear reference and produces identical source
  and screen geometry.
- Automated and live tests show correct behavior when configured screen aspect
  differs from the panel aspect.

## Dependencies and related work

- VP-0038 owns generic viewport state and aspect-driven NLS inputs.
- VP-0074 owns dynamic Alpha hook updates and shader cold-start behavior.
- VP-0083 owns anamorphic presentation ordering.
- VP-0089 owns the optional future two-axis balanced transform and depends on
  this story's geometry plan rather than replacing it.
- VP-0098 owns source-envelope selection and ordinary centered screen fit.

## Non-goals

- Projector lens-memory, zoom, masking motor, curtain, or madVR profile control.
- Calibration for deliberately offset, overscanned, or mechanically masked
  screens without explicit inset/offset configuration.
- Replacing VP-0089's balanced-warp design.
- Allowing NLS to decide source crop or presentation-envelope authority.
