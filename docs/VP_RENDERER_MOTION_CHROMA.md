# VP Renderer chroma and Motion configuration

Targets `v1.4.01-beta` and `dv2`; native HDR output is deferred.

## Scaling

`chroma_upscaler` belongs to each `[vprenderer.scaling]` profile. Missing values
resolve to `bicubic`, independently of rendering quality. There is no Auto value.
Supported values are `bilinear`, `bicubic`, `lanczos`, `ewa_lanczos`, and
`ewa_lanczossharp`. The selected filter is passed to libplacebo's plane_upscaler.
It reconstructs subsampled color planes even at native image size; RGB input
has no chroma reconstruction step. The existing sigmoid control is labeled
Sigmoid upscaling to distinguish it from a separate filter anti-ringing strength.

## Motion profiles

The compact Qt tab row adds Motion between Subtitles and Processing. The
Motion page uses the existing ordered profile, shortcut, and conditional-rule
model. Controls appear before shortcut metadata so they are visible at 1040x700.
Changing motion configuration restarts the renderer to discard temporal history.

```ini
[vprenderer.motion]
smooth_motion: off
frame_mixer: oversample
deinterlace: auto
deinterlace_algorithm: bwdif
field_order: auto
deinterlace_cadence: field
deinterlace_skip_spatial: false
```

- `smooth_motion`: `off`, `auto` (verified source/display cadence mismatch),
  or `always`. Default Off. Matching integer multiples bypass Auto mixing.
- `frame_mixer`: `oversample`, `hermite`, `linear`, or `cubic`.
  Default Oversample. This blends frames; it does not run RIFE/optical flow.
- `deinterlace`: `auto`, `off`, or `force`. Default Auto. Auto requires positive
  interlace metadata and known field order. Force still requires reported order
  or an explicit override; unknown order is not silently treated as top first.
- `deinterlace_algorithm`: `weave`, `bob`, `yadif`, or `bwdif`. Default BWDIF.
  Weave leaves the original frame intact. Bob is the missing-reference fallback.
- `field_order`: `auto` (From input), `top`, or `bottom`.
  DeckLink field dominance and completed Magewell-frame metadata are retained.
- `deinterlace_cadence`: `field` or `frame`. Default Field. Field mode preserves
  both fields when the display supports that rate; output is bounded by the
  measured display cadence. Frame mode selects one field-derived image/frame.
- `deinterlace_skip_spatial`: boolean, default false; applies to YADIF only.

## Runtime boundary

Off/progressive matched-rate output retains the single-image path without
allocating temporal history. Active motion uses up to eight private GPU frame
slots, recycled after warmup, and at most sixteen outputs per source interval.
The mixer receives monotonic relative PTS, stable frame signatures, and display
interval duration. YADIF/BWDIF receive ordered previous/current/next frame planes.
Mixing/deinterlacing dispatch through the existing NLS hook recovery contract.

History is cleared on source discontinuity, generation, scan/order policy,
substantial display-rate change, transfer change, swapchain reconstruction, and
backlog recovery. Source buffers are released through the original queue owner;
only copied GPU textures are kept. Missing texture capabilities/allocation
fall back to the ordinary path and report the reason.

Motion adds buffering: the OSD presentation status and log identify the active
method, field order and retained-frame delay. Audio alignment and live playback
quality require operator review; no live capture playback was changed during
implementation. Source subtitle cut/paste composition currently takes the safe
ordinary-rendering path and reports motion unavailable for that frame rather
than mixing unretained composed textures. Ordinary target overlays remain active.

## Validation

Tests cover configuration parsing/validation and persistence, exact libplacebo
option mapping, submitted field order, source texture ownership, generation
reset, bounded scheduling, successful GPU calls for every mixer/deinterlacer,
and navigation across all eight tabs. They do not evaluate libplacebo's visual
quality or rank its deinterlacing algorithms.
