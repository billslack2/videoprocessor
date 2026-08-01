# VP-0070-2: Alpha panel diagnostic overlay

## Status

Backlog. Depends on VP-0070-1.

## Parent

[VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

Connect the panel/glyph cue contract to Alpha. Add an opt-in diagnostic showing
the locked glyph bounds and a distinct panel/capture rectangle in the current
visible coordinate system, with truthful unavailable/candidate/stable state.

## Acceptance criteria

- Overlay is correct in normal and scope/CIH viewports and after renderer or
  generation transitions.
- Stable rectangles do not jitter during a cue.
- A stale result is never rendered and the normal OSD is unaffected.
- Diagnostics introduce no render-path wait, readback, or queue growth.

## Out of scope

Any source-pixel modification or destination subtitle rendering.

