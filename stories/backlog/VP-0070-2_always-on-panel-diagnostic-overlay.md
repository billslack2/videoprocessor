# VP-0070-2: Stable boundary-crossing diagnostic overlay

## Status

Backlog. Depends on the rebuilt VP-0070-1. The prior implementation in
`c6e251b`/`ccbc240` failed live validation: it painted unconfirmed candidates,
missed the real two-panel subtitle, and marked a large dark picture region as
glyphs. It must not be redeployed.

Build/test success is retained as regression history only; it did not validate
the detector's semantics.

Build-only checkpoint (2026-08-01): `0a4cefc` connects stable-only diagnostics
to both Alpha and DirectShow/madVR. The Alpha plugin proxy forwards the mode,
and the renderer plugin ABI was advanced to version 8 so an incompatible old
DLL fails closed. `highlight` paints each stable line's capture/glyph geometry
and mask; candidate, unavailable, stale, and generation-mismatched evidence
does not mutate the frame. The mode defaults to `off` at every API/state layer
and must be explicitly selected for testing. Clean x64 Release and 409/409
tests passed; live validation and deployment remain paused.

## Parent

[VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

Connect the rebuilt CueSet to both P010 output paths. Diagnostics are always on
for this validation build independently of `subtitle_reposition`, but only a
fresh `stable` CueSet may paint the normal panel/glyph overlay. Candidate,
stale, unavailable, OCR-only, and rejected results must remain visually
distinct and must never masquerade as captured geometry.
No build or deployment is authorized until VP-0070-1 meets its offline
boundary-crossing gates.

The offline review tool exposes an explicit `highlight` mode. It leaves the
source image unchanged and draws only the frozen cue geometry and qualified
glyph mask. This is the diagnostic half of the test-only `highlight`/`move`
mode toggle; it is not a production configuration switch yet.

## Acceptance criteria

- It is present on both Alpha and DirectShow/madVR test paths without changing
  active configuration or enabling legacy subtitle repositioning.
- Each line's stable rectangle does not jitter during a cue of any duration.
- Multi-line cues render separate panel/glyph regions, never a destructive
  union box.
- Picture-only, padding-only, and boundary-generation-mismatched cues
  render no normal detection overlay.
- A stale result is never rendered; generation changes reset the detector.
- Diagnostics introduce no render-path wait, readback, or queue growth.
- Live checks still need to cover normal and scope/CIH viewports, renderer
  switching, and sustained 60-fps capture performance.

## Out of scope

Source restoration and destination subtitle rendering. Optional detector-only
inference belongs to VP-0070-1 and is merely consumed here.
