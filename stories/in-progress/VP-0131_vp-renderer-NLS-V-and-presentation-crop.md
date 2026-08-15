# VP-0131: VP Renderer NLS-V and bounded presentation crop

## Status

In Progress (2026-08-15). Implementing on branch
`codex/vp0089-vp0131-nls-profiles` in worktree
`C:\Videoprocessor\vp\git-main\stories\.vp0089-vp0131-nls` together with
VP-0089. The implementation starts from GitHub's current default branch,
`v1.2.001-beta` at `74a6c7e`.

Readiness review confirmed that the existing GLSL hook already implements the
required vertical one-axis map through dynamic `warp_axis`; the missing pieces
are a `wider_only` selector, a named VP Renderer profile, and a presentation-
owned symmetric crop resolver. The generic Config parameter table already
expands with the page on the current default branch, so this story requires no
Config UI work or spike.

## User story

As an operator with a fixed 16:9 screen, I want an opt-in NLS-V profile for
scope content and an optional small symmetric crop that reduces the nonlinear
correction, while retaining the current complete-picture behavior everywhere
the option is disabled.

## Accepted product contract

- `NLS-V` is a new VP Renderer-only profile using the existing `NLS.glsl`
  transform with `aspect_direction: wider_only`.
- The current `narrower_only` default and explicit `any` behavior remain
  unchanged. Direction becomes a validated enum with `narrower_only`,
  `wider_only`, and `any`.
- Intentional presentation crop is available to every VP Renderer NLS member,
  including the existing standard/protected profiles and VP-0089 NLS+.
- The crop is not detector output. It is derived after the trusted/full-raster
  source rectangle is selected and is used only for final NLS presentation.
- Intentional crop and NLS+ are permanently out of scope for madVR/HLSL.
  DirectShow retains full-source one-axis NLS and never emulates either feature.
- No dedicated Config controls are added. The existing generic parameter table
  stores and edits the typed values.

## Configuration contract

Add these typed NLS fields:

| Field | Values | Default | VP Renderer meaning |
| --- | --- | --- | --- |
| `aspect_direction` | `narrower_only`, `wider_only`, `any` | existing typed-NLS default `narrower_only` | Restricts activation by source aspect relative to target. |
| `vprenderer_max_crop_percent` | `0` through `10`, per affected edge | `0` | Maximum intentional symmetric crop. Zero preserves current behavior. |
| `vprenderer_crop_preference` | `preserve_image`, `minimize_distortion` | `preserve_image` | Use the minimum crop needed to satisfy the stretch cap, or as much valid configured crop as possible. |

The corrective crop axis is derived, not configured:

- source narrower than target: crop equal top/bottom amounts; and
- source wider than target: crop equal left/right amounts.

Crop never crosses the target aspect. If the permitted crop cannot bring the
mapping within `max_stretch_ratio`, VP discards the tentative crop and uses the
existing complete-source safe fit.

The shipped NLS-V profile uses `max_stretch_ratio: 1.22`,
`vprenderer_max_crop_percent: 6`, and
`vprenderer_crop_preference: preserve_image`. For 2.39:1 to 16:9 this selects
approximately 4.6 percent from each side and leaves a 1.22x vertical map.

## Scope

1. Replace the Boolean production rule field with the validated direction
   enum while keeping legacy/default selection equivalent.
2. Add the GLSL-only NLS-V profile; do not add or modify an HLSL shader.
3. Add a pure, deterministic presentation-crop policy that operates on the
   already-selected NLS source rectangle and returns a separate rectangle.
4. Apply the result only after NLS is requested and before final mapping/layout
   publication. Do not mutate detector evidence, crop authority, subtitle
   evidence, or saved active-picture geometry.
5. Make integer rounding symmetric and deterministic: never exceed the crop
   cap, never cross the target, and round minimum-required crop upward only
   when it remains within the configured limit.
6. Add the fields to existing VP Renderer NLS demo/deployed members with zero
   defaults, and configure the new NLS-V member with the accepted bounded crop.
7. Update public configuration help and change-only diagnostics.

## Acceptance criteria

- Existing NLS output and fit decisions are unchanged with crop `0` and with
  new fields omitted.
- `wider_only` activates for stable scope-to-16:9 input and passes through
  narrower/equal content; `narrower_only` and `any` retain current behavior.
- On VP Renderer, 2.39:1 to 16:9 with the shipped NLS-V settings crops about
  4.6 percent per side and reports a 1.22x vertical mapping.
- `minimize_distortion` uses the greatest valid configured crop without
  crossing the target aspect.
- Crop applies only while an eligible NLS mapping is active. It never affects
  NLS off, waiting, direction bypass, invalid geometry, or failed safe fit.
- An inadequate crop allowance preserves the complete source and reports the
  established safe-fit reason.
- madVR/HLSL behavior, shaders, presentation ownership, and configuration
  output are unchanged; VP Renderer-only fields have no effect there.
- Pure policy/configuration tests cover both crop axes, both preferences,
  rounding, invalid settings, direction boundaries, and compatibility.
- A clean x64 Release build and live VP Renderer validation cover scope films,
  subtitles, pans, menus, and mixed-aspect transitions.

## Non-goals

- madVR/HLSL intentional crop, NLS+, target-fill, masking, zoom, or parity,
  including any future/deferred parity commitment.
- A configurable partial-height `target_fill_percent`; the selected viewport
  remains the target and crop only trades picture edge content for lower warp.
- Active-picture detection changes or reuse of `automatic_crop` for intentional
  picture removal.
- Dedicated Config controls or any Config layout redesign.

## Dependencies and references

- VP-0089, VP-0098, VP-0099, VP-0104, VP-0114
- `shaders/NLS.glsl`
- `src\VideoProcessor-Lib\NlsGeometryPolicy.*`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`
