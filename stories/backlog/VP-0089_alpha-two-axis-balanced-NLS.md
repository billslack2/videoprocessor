# VP-0089: Alpha two-axis balanced non-linear stretch

## Status

Backlog (2026-08-05). Implement an original, Alpha-renderer-only two-axis
non-linear stretch mode that can share an aspect-ratio adjustment between
horizontal expansion and vertical compression. This is a functional
enhancement inspired by publicly documented NLS+ behavior; it must not copy,
claim compatibility with, or reverse engineer any proprietary Envy algorithm.

## User story

As a scope-screen user, I want to fill a wider screen from narrower content
with a balanced nonlinear transform, so 16:9 sports or broadcast material can
look less distorted than a horizontal-only NLS stretch while remaining fully
dynamic through aspect-ratio changes.

## Current behavior and gap

VP's shipped Alpha GLSL hook, `shaders/NLS.glsl`, already supports an original
monotonic nonlinear mapping with center protection, curve, and dynamic
`stretch_ratio` inputs. Its runtime contract selects exactly one axis through
`warp_axis`: narrower content on a scope screen receives horizontal mapping;
wider content can receive vertical mapping. It cannot transform both axes in
the same frame.

The public Envy NLS+ setup guide describes a useful functional control model:
optional asymmetric crop/zoom, center stretch, horizontal and vertical edge
areas, horizontal and vertical strength, per-content-aspect settings, and
interpolation between configured aspects. It also warns that aggressive stretch
can create visible distortion during pans. The guide provides no source code or
transform formula.

## Scope

1. Add an Alpha-only `balanced` NLS mode that produces one inverse mapping of
   output UV coordinates into source UV coordinates and may modify both X and
   Y within the same frame. The transform must be continuous, monotonic on both
   axes, finite, bounded to the source rectangle, and preserve the configured
   output viewport/screen aspect.
2. Use the active-picture rectangle and selected screen viewport already owned
   by Alpha. Do not infer geometry from the whole capture raster when trusted
   active-picture state is unavailable; retain the current `Waiting`/safe
   behavior instead of stretching an uncertain image.
3. Support a compact public configuration model for a typed Alpha NLS rule:
   - `nls_mode = horizontal | vertical | balanced`;
   - optional normalized crop/zoom amounts for left, top, right, and bottom;
   - `center_stretch` from 0 to 100 percent;
   - `horizontal_area` and `vertical_area` from 0 to 100 percent, defining the
     edge regions where each nonlinear warp is concentrated; and
   - `horizontal_strength` and `vertical_strength` from 0 to 100 percent.

   Exact spelling may be refined to fit the existing shader-rule schema, but
   values must be explicit, validated, documented, and renderer-namespaced.
4. Define deterministic fill policy. The runtime must calculate the remaining
   required aspect correction after crop/zoom and distribute it across enabled
   axes. A 100-percent enabled strength means use no more than the amount
   required to meet the selected target; lower strengths may intentionally
   retain black bars. Never overshoot, crop unexpectedly, or stretch a scope
   passthrough image.
5. Make all geometry-dependent values dynamic GLSL hook parameters. A trusted
   aspect change, profile/hotkey change, or NLS state transition must update
   parameters without shader recompilation, cache churn, renderer restart, or
   a stale-frame flash.
6. Preserve the current single-axis mode as the compatible default. `balanced`
   is opt-in and Alpha-only; do not modify the madVR HLSL loader or its shader
   rules in this story.
7. Provide truthful OSD and log diagnostics: mode, source and target aspects,
   trusted active rectangle, crop/zoom, effective horizontal and vertical
   transforms, fill fraction, dynamic-hook update status, and why NLS is
   waiting, bypassed, safe-fit, or active.

## Prerequisites and dependencies

- VP-0034 and VP-0035: restart-free mixed-aspect behavior and robust
  low-latency active-aspect transitions must be accepted before aggressive
  mixed-aspect validation.
- VP-0081 and VP-0085: madVR-specific NLS work is not in this story's scope,
  but its geometry lifecycle findings must not be contradicted by shared active
  picture state.
- VP-0080 and VP-0083: preserve the established fail-safe active crop and
  Alpha viewport/anamorphic contracts.

## Validation

1. Add pure policy tests for 4:3, 16:9, 1.85, 2.00, 2.20, 2.35, and 2.40 input
   on 16:9 and scope targets. Verify no-op/passthrough, partial-fill, and
   full-fill outcomes, with exact output aspect and bounded source coordinates.
2. Add shader/hook tests or deterministic render fixtures proving simultaneous
   X/Y parameter updates, monotonic mapping, center protection, and no sampled
   coordinate outside the active source rectangle.
3. Confirm retained crop/zoom and area/strength settings interpolate smoothly
   across adjacent stable content aspects without renderer restart or shader
   recompilation.
4. Live test 16:9 sports and broadcast content on a scope screen, including
   horizontal pans, vertical camera movement, tickers, menu/advert transitions,
   and 2.35/IMAX mixed-aspect material. Verify active geometry changes do not
   create a transient squashed image, full-raster stretch, stale frame, or
   renderer restart.
5. Compare balanced, horizontal-only, and disabled modes for output geometry,
   rendering time, queue depth, frame drops/repeats, and latency. The feature
   must remain optional if the visual trade-off is not preferred.

## Non-goals

- Do not copy, decompile, derive from, or represent this as Envy NLS+.
- Do not add a neural model, face detector, motion analysis, or per-object
  semantic warp in this story.
- Do not add madVR two-axis NLS support, DirectShow presentation changes, or
  subtitle movement.
- Do not force P010 or alter the Alpha native-format ingress contract.

## References

- [madVR Envy NLS+ Setup Guide](https://madvrenvy.com/wp-content/uploads/Envy-Setup-Guide-NLS.pdf)
- `shaders/NLS.glsl`
- `src\\VideoProcessor-Lib\\vprenderer\\LibplaceboVideoRenderer.cpp`
- `src\\VideoProcessor-Lib\\microsoft_directshow\\MadVRShaderLoader.cpp`
- VP-0034, VP-0035, VP-0080, VP-0081, VP-0083, VP-0085
