# VP-0044: Alpha native OSD visible-picture anchoring and scaling

## Status

In progress. Implementation is on `codex/vp-0044-visible-osd`, based on the
current `v1.1.015-beta` integration branch. The Alpha native overlay now has a
single, unit-tested final-picture placement calculation; remaining work is
Release build and live viewport validation.

## Implementation evidence

- 2026-08-02: `VideoProcessor-VPRenderer` x64 Release built successfully.
- 2026-08-02: `VideoProcessor-Test` x64 Release built successfully; the four
  `NativeStatsOverlayPlacementTests` cases passed (full frame, scope,
  letterbox/pillarbox, and constrained small picture).
- Live Alpha validation on 16:9 and scope/CIH content remains pending before
  this story can move to review.

## User story

As an Alpha-renderer user, I want the native OSD to sit in the lower-right of
the visible picture, with a consistent inset and size relative to that picture,
so it does not occupy presentation black bars and remains useful on 16:9,
scope/CIH, and fitted content.

## Reported behavior

Alpha's native OSD is currently placed relative to the full destination frame.
In windowed and scope/CIH examples it can appear in the black border outside
the visible picture. The requested placement is the picture's lower-right
corner, inset approximately 75 pixels from the right and bottom edges, not the
swap-chain's lower-right corner.

The same request applies when the selected screen/viewport is scope/CIH: the
OSD must understand the selected screen aspect and final fitted picture area.

## Existing implementation boundary

The native Alpha OSD is blended by libplacebo as `PL_OVERLAY_COORDS_DST_FRAME`.
It currently derives its destination rectangle from the full `baseTarget`
texture and applies a fixed 100-pixel inset. It does not use Alpha's final
screen-profile target geometry, active-picture crop, or selected viewport.

This is an Alpha-only placement change. The madVR/DirectShow OSD remains under
its existing external renderer/window ownership.

## Required picture-rectangle contract

Define one renderer-owned **visible picture rectangle** in final destination
pixels. It must be the area where viewer-visible picture content is rendered,
after all Alpha geometry decisions, including:

1. swap-chain/output dimensions;
2. selected generic viewport/screen aspect (for example 16:9 or 2.35:1 CIH);
3. final fit, crop, safe-fit, pillarbox, and letterbox mapping;
4. NLS mapping and any output-aspect rule; and
5. a trusted active-picture crop when available and appropriate.

The OSD must use that final rectangle rather than independently reimplementing
partial source or screen-aspect arithmetic. It must not assume that all black
pixels are bars: app/UI content, fades, and dark scenes are valid picture.
When active-picture evidence is unavailable or not trusted, fall back safely to
the final rendered viewport rectangle rather than moving the OSD based on a
guess.

The rectangle must be computed in the same coordinate space used by the final
libplacebo overlay placement. It must update atomically with the render
geometry and never use stale dimensions across a resize, renderer rebuild,
viewport selection, content-aspect transition, or output mode change.

## Required placement and scaling behavior

1. Anchor the OSD panel at the visible picture rectangle's bottom-right.
2. Use a 75-pixel right and bottom inset in final output coordinates. If the
   panel cannot fit with that inset, clamp it fully inside the visible picture
   rectangle; never place it in a black border or outside the frame.
3. Size/rasterize the native OSD relative to visible-picture dimensions, not
   just the fixed swap-chain size. Establish a documented reference scale (for
   example visible-picture height at 1080p) and a sensible minimum/maximum so
   text remains readable without becoming disproportionate on small previews
   or very large output.
4. Scale panel padding, text layout, and background together. Do not stretch a
   previously rasterized bitmap independently in X/Y or make the panel blurry.
5. Preserve alpha blending, OSD text content, Ctrl+I visibility behavior, and
   current dynamic panel-height behavior.
6. Geometry changes must update placement/scale live without a renderer
   restart, presentation-contract switch, or visible flash.
7. The OSD may overlap picture content by design; it must not use black bars
   merely to avoid overlapping content.

## Scope/CIH requirements

- On a selected 2.35:1/CIH viewport, a fitted 16:9 input must place the OSD
  inside the actual 16:9 visible picture, not in the surrounding scope-frame
  black area.
- On matching scope content, place the OSD within the scope picture with the
  same lower-right inset.
- On a selected 16:9 viewport, use the visible 16:9 picture area regardless
  of the source raster's container.
- Content-aspect transitions and NLS passthrough/stretch/safe-fit changes must
  reposition the OSD coherently, without anchoring it to a previous aspect's
  rectangle.

## Diagnostics

Log only on placement/scale changes:

- renderer generation and source sequence/geometry generation;
- output frame rectangle;
- selected viewport profile and normalized screen aspect;
- final rendered visible-picture rectangle and whether trusted active-picture
  evidence refined it;
- OSD bitmap size, scale factor, final panel rectangle, and inset/clamp result;
- reason for fallback when active-picture evidence is unavailable.

Provide enough data to reconstruct placement from a screenshot without
per-frame logging.

## Verification

1. Unit-test rectangle and scaling calculations for full-frame 16:9, 2.35
   scope, 4:3, letterboxed, pillarboxed, safe-fit, and small-window cases.
2. Test 16:9 input on 16:9 and 2.35 viewports, matching 2.35 content on a
   scope viewport, and 4:3 safe-fit behavior.
3. Test trusted and unavailable active-picture evidence; unavailable evidence
   must use stable final viewport geometry without false bar detection.
4. Exercise NLS mapping changes, F2/F3 viewport selection, renderer resize,
   windowed/fullscreen transitions, display-mode changes, and renderer rebuilds.
5. Confirm the OSD remains fully inside the visible picture with a 75-pixel
   inset where space permits, has proportional readable text, and does not
   flicker or cause a render restart.
6. Confirm Alpha native OSD and madVR/DirectShow legacy OSD visibility handoff
   remains correct.

## Acceptance criteria

- Alpha native OSD is always anchored inside the final visible picture area,
  not the swap-chain's unused black border.
- It respects selected generic viewport/screen settings, including scope/CIH.
- The lower-right panel has the requested 75-pixel inset when geometry allows
  and remains fully contained when it does not.
- OSD size and layout scale coherently with the visible picture.
- Placement changes are restart-free, diagnostically traceable, and do not
  regress output geometry, NLS, subtitle fitting, or Alpha OSD blending.

## Dependencies

Builds on VP-0037's native Alpha OSD ownership and VP-0038's generic viewport
state. It should reuse trusted active-picture/geometry work where available;
it must not introduce a separate unsafe black-bar detector.
