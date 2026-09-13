# VP-0186: Add edge padding to Screen Config picture alignment

## Status

Backlog

Created 2026-09-13 at the user's request. Implementation has not started.

## User story

As a VideoProcessor operator using a Top or Bottom Screen Config picture
alignment, I want to reserve a small, explicit empty edge area in pixels, so I
can shift the fitted picture inward from that selected edge without changing
its crop, scale, or aspect.

## Required behavior

- Add a `vertical_alignment_padding` Screen Config setting, expressed in source
  pixels and defaulting to `0`. It is an edge inset, not added picture content:
  the reserved area remains black/empty.
- When `vertical_alignment` is `top`, position the fitted picture that many
  pixels below the top screen edge. When it is `bottom`, position the picture
  that many pixels above the bottom screen edge.
- When alignment is `center`, edge padding has no effect. The configuration UI
  disables the field in this state, while retaining its saved value for use if
  the operator later selects Top or Bottom.
- The zero/default path must be presentation-compatible with current Top,
  Center, and Bottom behavior. Center remains the default alignment.
- Put the field immediately below **Vertical picture alignment** in the
  existing expanded **Screen geometry** section. Reuse the existing config
  editor's fixed-unit inline form treatment: label **Edge padding**, numeric
  input, and a `px` unit label; do not redesign the Screen Config page.
- Help text must explain that the empty space occurs at the selected Top or
  Bottom edge and that the field is unavailable for Center.
- Apply the resolved inset after normal picture fitting and alignment, without
  changing the physical screen rectangle, source crop, scale, aspect,
  automatic crop policy, NLS authority, or subtitle-fit safety behavior.
  Define and test the precedence if subtitle fitting must temporarily move the
  picture to keep subtitle pixels visible.
- Validate a non-negative finite integral pixel value with a documented upper
  bound derived from the configured screen/output geometry. Reject invalid
  values clearly; do not silently reinterpret a negative value as the opposite
  edge.
- Preserve inheritance, profile switching, round-trip configuration edits,
  live-apply/restart behavior, public configuration reference, accessibility
  names, and renderer diagnostics using the established Screen Config
  conventions.

## Acceptance criteria

1. A missing setting and an explicit `0` produce identical output for all
   three alignment choices.
2. With Top and a positive value, the fitted picture is inset from the top by
   that amount and the space above it remains empty; with Bottom, the same is
   true at the bottom.
3. Center keeps the picture centered, ignores the saved value, and presents a
   disabled Edge padding field in the editor. Changing back to Top or Bottom
   restores the retained value.
4. Geometry tests cover zero, valid positive values, values near the maximum,
   invalid negative/non-finite/out-of-range input, source/target aspect cases,
   NLS active/inactive, and subtitle-fit precedence.
5. Config editor tests cover defaulting, reload/round-trip, profile inheritance,
   enablement state, fixed `px` units, help/accessibility text, and live apply
   behavior.
6. Relevant automated tests and an x64 Release build pass. Do not deploy
   without an explicit request.

## Design reference

The requested mockup retains the established dark VideoProcessor Config visual
language and shows the field in its exact proposed Screen geometry placement:
Center with a disabled `0 px` field, and Top with an enabled `48 px` field and
empty space above the picture. It is a review aid, not a replacement for the
existing UI styling or layout.

## Dependencies and next action

Before implementation, complete the normal readiness review against the
current beta integration tip: trace the existing `vertical_alignment` setting
through schema, config editor, profile resolution, renderer layout, subtitle
fit, NLS, diagnostics, and public configuration documentation. Confirm the
appropriate pixel coordinate space and maximum safe inset before finalizing the
validation rule.

## Out of scope

Horizontal alignment/padding, symmetric padding, new crop or scaling modes,
altering the source picture, and changing existing alignment defaults.

## Evidence locations

- `src/VideoProcessor-Config/ConfigEditorWindow.cpp`
- `src/VideoProcessor-Lib/RendererProfileConfig.h`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- `src/VideoProcessor-Test/AlphaSourceCropPolicyTests.cpp`
- `src/VideoProcessor-ConfigTests/ConfigEditorWindowTests.cpp`
- `CONFIGURATION.html`
