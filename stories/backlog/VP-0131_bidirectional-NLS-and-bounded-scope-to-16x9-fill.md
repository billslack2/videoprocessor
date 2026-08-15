# VP-0131: Bidirectional NLS and bounded scope-to-16:9 fill

## Status

Backlog (2026-08-15). Created from the request to use Protected NLS on a
fixed 16:9 screen for 2.35:1/2.39:1 material, optionally trading a small,
bounded, symmetric side crop for a milder warp. No implementation branch,
source change, configuration change, or deployment has started.

The NLS direction-only path already has both GLSL and HLSL support for a
runtime vertical warp. The new bounded side-crop and target-fill presentation
path is VP Renderer/libplacebo-only in this story. It must not be represented
as a madVR feature until a separate DirectShow presentation/crop design has
been implemented and live-validated.

## User story

As an operator with a fixed 16:9 screen, I want an opt-in Protected NLS mode
for scope content that can fill all or a selected proportion of the screen,
while retaining as much of the active picture as possible and using a small,
explicit side crop only when it materially reduces visible nonlinear
distortion.

I also want a generic NLS rule to work in either direction: 16:9 content may
expand toward a selected scope screen, while scope content may map toward a
selected 16:9 screen.

## Current behavior and gap

The existing NLS contract derives source aspect from the trusted active picture
and target aspect from the selected viewport. `aspect_direction: any` can
already choose horizontal mapping for a source narrower than its target and
vertical mapping for a source wider than its target. Both `NLS.glsl` and
`NLS.hlsl` receive the selected axis.

The shipped direction model has only `narrower_only` and `any`; it cannot
express an opt-in scope-to-16:9-only preset. More importantly, current
`automatic_crop` removes only trusted encoded bars. It must never discard real
left/right picture content, and there is no intentional presentation-side crop,
target-fill setting, or crop-versus-distortion policy.

For a 2.39:1 active picture and a 16:9 target, full fill requires a 1.3444x
vertical correction. With a 1.22x requested limit, the smallest symmetric crop
that permits full fill is about 4.6% from each active-picture side. A 6% crop
would reduce the correction to about 1.18x. These are presentation choices,
not active-picture detector results.

## Proposed configuration contract

Existing NLS fields remain unchanged: `quality`, `geometry`, `strength`,
`center_protection`, `curve`, `tolerance_percent`, and
`max_stretch_ratio`.

Add the following NLS-rule fields:

| Field | Values / range | Default | Meaning |
| --- | --- | --- | --- |
| `aspect_direction` | `narrower_only`, `wider_only`, `any` | `narrower_only` | `wider_only` activates only when the trusted source is wider than the resolved target. `any` remains bidirectional. |
| `target_fill_percent` | 75 through 100 | 100 | VP Renderer only. The desired final picture height as a percentage of the selected viewport height. The picture remains horizontally fitted and centered. |
| `max_side_crop_percent` | 0 through 10, per side | 0 | VP Renderer only. Maximum intentional equal inset from the trusted active picture's left and right edges. `0` disables intentional side crop. |
| `crop_preference` | `preserve_image`, `minimize_distortion` | `preserve_image` | VP Renderer only. Selects the minimum crop needed to satisfy the NLS cap, or the greatest valid crop up to the configured cap for the mildest mapping. |

The canonical scope-fill preset is conceptually:

```ini
[vprenderer.viewport.scope_fill_16x9]
label: Scope Fill 16:9
screen_aspect: 16:9
automatic_crop: true
vertical_alignment: center

[shader.nls.scope_fill_16x9]
shader_type: nls
label: Scope Fill Protected
stage: pre_resize
hlsl_file: NLS.hlsl
glsl_file: NLS.glsl
quality: high
geometry: protected
strength: 1.0
center_protection: 0.35
curve: 2.0
tolerance_percent: 5
aspect_direction: wider_only
max_stretch_ratio: 1.22
target_fill_percent: 100
max_side_crop_percent: 6
crop_preference: preserve_image
```

This is a proposed contract, not a currently accepted configuration. The
configuration editor must preserve its existing generic parameter-table model;
the new fields do not require dedicated form controls.

## Scope

1. Replace the Boolean NLS direction model with a shared, validated direction
   enum: `narrower_only`, `wider_only`, and `any`. Preserve the existing
   default and `any` behavior. Apply direction-only parity to both VP Renderer
   GLSL and DirectShow/madVR HLSL paths, with pure mapping-policy and backend
   selection tests.
2. Implement VP Renderer-only target-fill and intentional symmetric side-crop
   resolution after trusted active-picture/full-raster source geometry is
   selected and before final NLS mapping/layout is published. It must be a
   presentation-owned rectangle; it must not alter detector output, crop
   authority, subtitle evidence, or the saved active-picture envelope.
3. Solve crop and NLS together. For a source wider than the effective target,
   `preserve_image` chooses the least equal side inset that makes the requested
   correction no greater than `max_stretch_ratio`; `minimize_distortion` may
   use up to `max_side_crop_percent` while keeping a valid wider-than-target
   mapping. If the requested fill cannot meet all limits, preserve the source
   with the established safe-fit behavior and a specific diagnostic reason.
4. Define `target_fill_percent` exactly as final viewport-height occupancy.
   Apply it to the effective target aspect and centered destination rectangle
   coherently, so it changes neither the physical `screen_aspect` contract nor
   the meaning of an ordinary NLS-off fit.
5. Pass the resolved presentation rectangle and mapping as one per-frame
   decision to the GLSL hook, final source crop, destination layout, OSD, and
   change-only diagnostics. The existing one-axis Protected NLS math should be
   reused; do not add a two-axis or semantic warp.
6. Treat `target_fill_percent`, `max_side_crop_percent`, and
   `crop_preference` as VP Renderer-only presentation fields. DirectShow/madVR
   must accept a shared rule containing them, ignore them, and retain its
   current full-source direction-only NLS behavior. Log that fact once per
   effective rule/backend change; do not reject the rule, silently simulate a
   crop, or claim presentation-crop parity. The shared
   `max_stretch_ratio` still evaluates the unmodified DirectShow source, so a
   2.39:1-to-16:9 rule capped at 1.22x correctly takes existing safe fit there.
   A future parity story must explicitly solve madVR-owned crop and
   output-aspect coordination.
7. Make the sole requested Config UI change: remove the internal vertical
   scrolling/height cap from the custom parameter table. Its rows must expand
   the page, and the window's normal outer scroll area must carry overflow.
   Preserve table selection, add/remove behavior, keyboard navigation,
   validation, comments, and unknown parameter round-tripping. Do not add
   dedicated controls for the new NLS fields in this story.
8. Update the public configuration reference and UI help to distinguish
   detector-owned automatic crop from explicit presentation-side crop, identify
   backend availability, and show the scope-fill example.

## Acceptance criteria

- The default configuration produces byte-for-byte-equivalent NLS selection
  and ordinary fit behavior when all new settings retain their defaults.
- `wider_only` engages only for a stable source wider than its resolved target;
  it does not NLS-expand 4:3 or 16:9 material toward a 16:9 viewport.
  `narrower_only` and `any` retain their documented behavior.
- Both GLSL and HLSL prove the direction-only 16:9-to-2.35 horizontal and
  2.39-to-16:9 vertical policy cases, including tolerance and safe-limit
  boundaries. The two shipped directions remain available to a generic `any`
  rule.
- On VP Renderer, 2.39:1 to a 16:9 viewport with 100% fill,
  `max_stretch_ratio: 1.22`, `max_side_crop_percent: 6`, and
  `crop_preference: preserve_image` crops approximately 4.6% per side and
  reports a 1.22x vertical mapping. `minimize_distortion` may use 6% per side
  and reports the lower resulting ratio. Exact integer-pixel rounding is
  deterministic and documented.
- A 90% target fill can use the appropriate reduced effective correction
  without intentional crop when it is within the configured ratio limit.
- Intentional side crop applies only to the source-wider-than-target path. It
  never affects 16:9-to-scope expansion, detector-owned bar removal, or NLS
  off/safe-fit output.
- Inadequate crop allowance, unstable/malformed geometry, an invalid setting,
  or a target beyond the shader safety bound retains the complete source and
  produces a truthful reason in diagnostics.
- DirectShow/madVR remains correct for direction-only rules. A shared rule
  containing VP Renderer-only crop/fill fields remains valid: HLSL ignores
  those fields, logs that it did so once per effective rule/backend change,
  and uses the full source plus the common stretch limit. Thus a 2.39:1 to
  16:9 rule capped at 1.22x safely fits in DirectShow, while the same rule can
  crop and engage at 1.22x in VP Renderer; a common 1.4x cap permits the
  existing full-source DirectShow vertical NLS path.
- The Config custom-parameter table grows to show every parameter row without
  an internal vertical scrollbar. The containing page/window scrolls normally,
  and focused UI tests cover long lists, add/remove, keyboard use, save/reload,
  and unknown-key preservation.
- Add unit/policy tests, configuration validation tests, VP Renderer geometry
  and transition tests, shader-hook tests, DirectShow direction-only tests,
  focused Config UI tests, a clean x64 Release build, and live VP Renderer
  validation with scope films, menus, subtitles, pans, and mixed-aspect
  transitions.

## Non-goals

- DirectShow/madVR intentional side crop or target-fill implementation.
- Changing madVR crop, zoom, masking, projector lens, or screen ownership.
- Replacing `automatic_crop` or allowing NLS to become a second active-picture
  detector.
- A two-axis/balanced NLS mode; that remains VP-0089.
- Dedicated per-field Config controls or a general configuration-editor
  redesign. The generic parameter table is sufficient once it no longer has
  its own scrolling region.

## Dependencies and references

- VP-0099: shared NLS geometry/safety policy and backend mapping contract.
- VP-0098 and VP-0104: trusted/fallback source geometry and viewport target
  rules.
- VP-0114: bounded small-bar zoom is related presentation work but remains
  independent; it must not absorb NLS-specific crop behavior.
- VP-0089: balanced two-axis NLS remains separate.
- VP-0113: coordinate any shared Config layout change with its in-progress
  screen-layout work; do not alter its scope or worktree.
- `shaders/NLS.glsl`, `shaders/NLS.hlsl`, `NlsGeometryPolicy`, VP Renderer
  final layout, and the Config custom-parameter table.
