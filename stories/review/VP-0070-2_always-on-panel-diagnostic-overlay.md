# VP-0070-2: Always-on panel diagnostic overlay

## Status

Review. Depends on VP-0070-1. Implemented in `c6e251b` (`Add always-on panel
subtitle diagnostics`) on `codex/vp-0070-1-panel-detection`, rebased onto
local VP-0066 `919819a` before this iteration.

The x64 Release GUI, DirectShow/madVR output component, and Alpha renderer
builds succeeded; all 384 `VideoProcessor-Test` tests passed. The Release
diagnostic build was deployed on 2026-08-01 to `C:\\Videoprocessor\\vp` with
the previous executable and renderer DLL retained as `.bak` files. Active
configuration was deliberately untouched: legacy `subtitle_reposition` remains
absent/disabled, so its OCR/DirectML path is not enabled.

## Parent

[VP-0070](../backlog/VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

Connect the panel/glyph cue contract to both P010 output paths: Alpha and
DirectShow/madVR. For this diagnostic test build it is always on, independently
of `subtitle_reposition`, and paints a magenta panel border, yellow glyph
rectangle, and cyan glyph-mask tint into the frame after existing scene/NLS
analysis and before output/upload. It uses no OCR, ONNX, DirectML, or neural
model. The generic detector contract freezes stable geometry indefinitely until
the cue fingerprint or generation changes.

## Acceptance criteria

- It is present on both Alpha and DirectShow/madVR test paths without changing
  active configuration or enabling legacy subtitle repositioning.
- Stable rectangles do not jitter during a cue of any duration.
- A stale result is never rendered; generation changes reset the detector.
- Diagnostics introduce no render-path wait, readback, or queue growth.
- Live checks still need to cover normal and scope/CIH viewports, renderer
  switching, and sustained 60-fps capture performance.

## Out of scope

Source restoration, destination subtitle rendering, OCR, ONNX, DirectML, and
neural inference.
