# VP-0103: Configurable vertical picture alignment with subtitle-aware fit

## Status

Backlog (2026-08-08). The requested behavior and configuration/UI contract are
recorded. Implementation has not started. Before moving this story to In
Progress, complete the tracker implementation-branch gate and confirm the
current VideoProcessor integration base.

## User story

As a user displaying scope or other wide content on a taller screen, I want
each viewport profile to place the fitted picture at the top, center, or bottom
of the visible screen so I can use one-sided physical masking, while the
existing HDMI subtitle-fit behavior can still shift the whole picture in
either direction when subtitle pixels appear in or across an encoded black
bar.

## Context

VideoProcessor receives the program and subtitles together as HDMI video
pixels. It does not own a subtitle track or separately render subtitle glyphs.
Subtitles may be entirely inside the detected active picture, entirely in an
encoded top or bottom bar, or cross a detected picture boundary.

The current Alpha `subtitle_fit` behavior can move the complete presentation
to keep required subtitle pixels visible. This story adds a configurable
*resting* vertical placement. It must generalize the existing displacement
behavior rather than add a second subtitle detector or assume subtitles occur
only below the picture.

The primary use case is scope content on a 16:9 screen with one-sided masking:

- `top` places all otherwise unused vertical screen space below the picture;
- `center` preserves the current equal-bar presentation; and
- `bottom` places all otherwise unused vertical screen space above the
  picture.

On a CIH screen where the fitted presentation already consumes the available
height, the alignment selection has no resting-position effect. Existing
subtitle-fit displacement must continue to operate within the space available.

## Required geometry contract

Vertical alignment is destination placement, not source cropping. It must not
translate, contract, or invent a source crop and must not acquire active-picture
authority.

For the generation-current presentation envelope and physical screen:

1. Select the trusted source presentation envelope under the existing crop and
   bounded bar-content rules.
2. Apply the existing aspect-preserving, anamorphic, or approved NLS mapping to
   determine the final picture size.
3. Place that picture at its configured resting alignment within the physical
   screen rectangle.
4. Apply the existing subtitle-fit policy relative to that resting rectangle.
   Required top pixels may shift the whole presentation downward and required
   bottom pixels may shift it upward.
5. Use the minimum displacement that keeps the required presentation envelope
   visible, clamp the result to the physical screen, retain the existing hold
   behavior, and return to the configured resting alignment after release.

The three resting positions for a fitted picture rectangle of height `H`
inside screen rectangle `[screenTop, screenBottom)` are:

```text
top:    y = screenTop
center: y = screenTop + (screenHeight - H) / 2
bottom: y = screenBottom - H
```

Existing integer rounding and chroma-alignment policy owns the exact pixel
boundary. No mode may change picture scale merely to obtain a different
resting position. When no vertical slack exists, all three modes resolve to
the same rectangle.

## Subtitle-fit interaction

- Subtitle evidence remains encoded-video evidence from the shared/current
  analysis path; do not introduce OCR or a renderer-generated subtitle model.
- A subtitle wholly inside the active picture requires no additional shift.
- A lower-bar or lower-boundary subtitle may move a top-, center-, or
  bottom-aligned presentation upward by the minimum required amount.
- A top-bar or top-boundary subtitle may move any resting alignment downward by
  the minimum required amount.
- Alternating top and bottom subtitles must not inherit a stale displacement,
  oscillate between unrelated candidates, or manufacture an opposite source
  edge.
- Existing subtitle padding, hold, release, generation invalidation, and
  fail-safe behavior remain authoritative.
- If current required pixels cannot all fit inside the screen without an
  unsupported crop or scale change, preserve the current fail-safe behavior
  and report the constrained result; never silently discard required pixels.

## Configuration contract

Add a finite per-viewport setting:

```text
[profiles.viewport.scope_on_16x9]
screen_aspect: 16:9
vertical_alignment: top
subtitle_fit: true
subtitle_hold_seconds: 2
subtitle_padding_pixels: 30
```

- Accepted values are `top`, `center`, and `bottom`, case-insensitively if that
  matches the shared enum parser convention.
- The default is `center`, preserving every existing configuration and current
  presentation result.
- Reject unsupported values with a section/key-specific validation error.
- Resolve the value into the same coherent viewport snapshot as
  `screen_aspect`, `subtitle_fit`, subtitle hold, padding, anamorphic scale, and
  viewport generation.
- Viewport/profile changes must apply through the existing live selection path
  without a renderer restart when the current renderer supports live final
  placement.
- Profile names remain labels and must not imply an alignment.
- Document the setting in the canonical configuration reference and generated
  field inventory.

## Configuration editor requirements

- Expose **Vertical picture alignment** on each viewport detail page as a
  three-value selector: **Top**, **Center**, and **Bottom**.
- Default newly created viewports to **Center**.
- Explain that this controls the picture's resting position within unused
  vertical screen space and that enabled subtitle fitting may temporarily move
  it away from that edge.
- Load, edit, validate, save, and reopen all three values without changing
  unrelated configuration text or values.
- Preserve the existing safe-save, external-change detection, validation, and
  rollback guarantees owned by VP-0097.

## Renderer boundary

Alpha owns its final libplacebo viewport and is the required implementation
target for this story.

VP does not currently have a documented madVR control that can move madVR's
completed presentation without pixel remapping or a renderer restart. This
story must not implement alignment or subtitle displacement through a madVR
translation shader. When madVR is active, retain its independently configured
placement and make the unsupported VP alignment state clear in diagnostics or
UI. VP-0087 remains the blocked story for VP-managed subtitle-fit placement
with madVR.

## Diagnostics and OSD

Expose enough state to distinguish the resting policy from a temporary
subtitle displacement:

- selected viewport and `vertical_alignment`;
- physical screen rectangle;
- fitted resting picture rectangle;
- current subtitle-fit direction and displacement in pixels;
- final presented rectangle; and
- unsupported-renderer or constrained-placement reason when applicable.

Routine logs should report transitions rather than emit the same placement on
every frame. Existing OSD geometry must follow the final presented picture
rectangle where its current contract requires picture-relative placement.

## Verification

Add deterministic geometry and configuration tests covering:

1. Top, center, and bottom placement of 2.35 and 2.40 content on 16:9 screens.
2. Center-default compatibility for configurations with no new key.
3. Equal-height/CIH cases where all alignments produce the same resting
   rectangle.
4. Content narrower than the screen, where there is no vertical slack and the
   setting must not introduce vertical movement.
5. Bottom-bar, bottom-boundary, top-bar, and top-boundary subtitle evidence
   from every resting alignment.
6. Minimum bidirectional displacement, screen clamping, subtitle padding,
   hold/release, and return to the selected resting edge.
7. Alternating top/bottom evidence, scene changes, viewport changes, renderer
   epochs, resolution changes, and evidence withdrawal without stale offsets.
8. Automatic crop on and off, linear fit, anamorphic profiles, and supported
   NLS mappings without source-pixel loss or an unintended scale change.
9. Parser acceptance/rejection, resolved viewport publication, configuration
   round trips, editor defaults, and preservation of unrelated configuration.
10. Clear non-application under madVR with no generated translation shader.

Complete the native tests, focused configuration-editor tests, and a clean x64
Release solution build. Live Alpha validation must include a scope program on
a 16:9 screen with one-sided masking behavior and real HDMI subtitles at the
bottom, across a lower boundary, at the top, and across an upper boundary.

## Acceptance criteria

- A viewport can select top, center, or bottom resting alignment in both the
  configuration file and configuration editor.
- Omitting the setting exactly preserves centered behavior.
- Top and bottom placement redistribute only unused vertical destination
  space; they do not crop, rescale, stretch, or alter source authority.
- Existing subtitle fitting can temporarily shift the complete presentation
  upward or downward from any resting alignment and returns to that alignment
  after the configured hold/release behavior.
- Required HDMI subtitle pixels wholly in a bar or crossing either picture
  boundary remain visible whenever the existing screen-fit contract can
  contain them.
- Viewport hotkey/profile changes update alignment coherently with aspect and
  subtitle settings without stale placement.
- Unsupported madVR placement is explicit and does not use a translation
  shader or silently claim the Alpha behavior.
- Automated tests, the clean x64 Release build, and live Alpha validation pass.

## Dependencies and related work

- VP-0038 owns generic viewport state, configuration, and restart-free
  selection.
- VP-0080 owns fail-safe Alpha crop authority and subtitle evidence boundaries.
- VP-0098 owns the trusted presentation envelope and arbitrary-screen fit.
- VP-0097 owns the standalone configuration editor and safe round-trip edits.
- VP-0087 remains blocked for equivalent VP-managed placement under madVR.
- VP-0070 owns future subtitle capture/relocation and is not required here.

## Non-goals

- OCR, glyph extraction, subtitle replacement, or separate subtitle rendering.
- Moving physical masks, curtains, a projector lens, or screen hardware.
- Adding horizontal alignment or arbitrary pixel offsets.
- Reconfiguring madVR or implementing placement with a translation shader.
- Changing active-picture detection thresholds or granting subtitle evidence
  crop authority.
