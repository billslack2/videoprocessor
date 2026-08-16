# VP-0131: VP Renderer NLS-V and bounded presentation crop

## Status

Review (2026-08-15). Implemented on branch
`codex/vp0089-vp0131-nls-profiles` together with VP-0089 and submitted as
GitHub PR [#64](https://github.com/billslack2/videoprocessor/pull/64) against
`v1.2.001-beta`. Live picture-quality acceptance remains a release-review
item; the code and configuration-editor changes are ready for review.

Readiness review confirmed that the existing GLSL hook already implements the
required vertical one-axis map through dynamic `warp_axis`; the missing pieces
are a `wider_only` selector, a named VP Renderer profile, and a presentation-
owned symmetric crop resolver. The generic Config parameter table already
expands with the page on the current default branch, so this story requires no
Config UI work or spike.

Implementation checkpoint (2026-08-15): committed as `847c865` on the
feature branch. Production NLS direction is now the explicit
`narrower_only`/`wider_only`/`any` enum; a separate deterministic crop policy
returns presentation-only geometry; VP Renderer applies it after detector/full-
raster source selection; and the new GLSL-only NLS-V profile reuses
`NLS.glsl`. Crop remains zero by default on existing members and is not read by
the madVR presentation path.

Verification checkpoint: the clean x64 Release solution build passed; all 41
focused NLS/configuration tests passed, including both crop axes, both crop
preferences, inadequate-crop fallback, wider-only selection, and unchanged
HLSL behavior; the separate Config application suite passed; and release
packaging staged and verified all 56 immutable files. The full native suite
passed 832 of 837 tests; its five failures are pre-existing stale default-
branch documentation/config expectations unrelated to NLS. Live VP Renderer
scope/subtitle/pan/menu/mixed-aspect acceptance remains open as a release-review
item.

Initial deployment checkpoint: the clean `847c865` executable/VP Renderer DLL
pair, Config binaries, new shader members, bounded-crop settings, demo config,
and active config were installed at `C:\Videoprocessor\vp`. That deployment
incorrectly included an NLS-specific viewport, which the user removed and
which must not be restored. The prior files are recoverable from
`C:\Videoprocessor\vp\backups\vp0089-vp0131-20260815-201830`.

Runtime QA correction (2026-08-15): the latest failed visual run selected
NLS-V while retaining a full 16:9 raster, so a detected 2.00:1 picture was
incorrectly evaluated as 16:9-to-16:9 passthrough. The rejected solution was a
new/coupled 16:9 viewport profile. The accepted correction restores the
established VP-0003 NLS contract instead: whenever VP Renderer NLS is selected,
it maps the trusted active-picture rectangle (thereby removing detected encoded
bars) independently of the viewport's ordinary `automatic_crop` setting.
NLS-off presentation remains unchanged. The optional percentage is then only
an additional crop of actual picture pixels when required by the stretch cap.
The demo and deployed configurations must contain only the NLS+ and NLS-V
shader members—no NLS-specific viewport and no new renderer.

QA also found that a later local operation replaced only the deployed EXE,
breaking the tested EXE/plugin pairing. A fresh x64 Release pair must be
backed up, deployed together, and hash-verified before live acceptance. New
change-only telemetry records detector versus presentation geometry, NLS
active-picture crop, bounded additional crop, bound hook values, installed
hook count, and render success. A real WARP readback test proves the existing
GLSL vertical path changes only Y pixels and preserves X.

Final QA deployment checkpoint: the correction is committed as `cf2d8fc`.
The clean x64 Release rebuild reported `VERSION_DIRTY=false`; all 45 focused
NLS/GLSL/configuration tests passed. The matched deployment hashes are
`30AF5CD5...E4245C` for `VideoProcessor.exe` and
`462C27FC...A385E` for `VideoProcessorVPRenderer.dll`. The corrected example
config contains only NLS+ and NLS-V shader members and no NLS-specific
viewport. The active user config was not modified. The replaced files are
recoverable from
`C:\Videoprocessor\vp\backups\vp0089-vp0131-qa-20260815-214708`.

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
- NLS first uses the trusted active-picture rectangle, preserving the
  established 4:3-to-16:9 and bar-removal behavior without a special viewport.
  Intentional crop is not detector output; it is derived afterward and used
  only for final NLS presentation.
- Do not create an NLS-specific 16:9 viewport or a new renderer. NLS-V is only
  a GLSL shader member selected through the existing dynamic shader mechanism.
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
   trusted active-picture rectangle after encoded bars have been removed and
   returns a separate rectangle.
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
- Stable detected bars are removed as part of the established NLS source
  contract even when ordinary viewport `automatic_crop` is off; NLS-off keeps
  the viewport/full-raster presentation unchanged.
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
  rounding, invalid settings, direction boundaries, compatibility, and a real
  libplacebo/D3D11 WARP pixel-readback of the checked-in GLSL.
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
