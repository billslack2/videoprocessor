# VP-0010: Strict bar-only OCR subtitle replacement

## Status

Draft.

## User story

As a viewer whose source places burned-in subtitles in the lower encoded
letterbox bar, I want VP to recognize and redraw only those bar-contained
captions as larger, consistently positioned text over the visible image. VP
must leave captions that touch the active picture completely untouched.

This targets sources configured like Apple TV where captions are deliberately
placed in black bars. It must support centered, one-line and multi-line captions
including speaker labels and line breaks.

## Context

The existing DirectShow subtitle relocation implementation is in:

`src\VideoProcessor-Lib\microsoft_directshow\live_source_filter\CBufferedLiveSourceVideoOutputPin.cpp`

Its `BASIC` and `ADVANCED` modes were designed to handle subtitles that cross
the picture/bar boundary, translucent source overlays, and other difficult
cases. `ADVANCED` combines geometry detection, temporal glyph tracking, and
pixel-mask relocation. This story deliberately does not simplify or replace
those existing modes.

The existing Windows OCR detector is in:

`src\VideoProcessor-Lib\microsoft_directshow\live_source_filter\WindowsOcrSubtitleDetector.cpp`

It already:

- recognizes text in bands around the detected picture boundaries;
- retains line text internally while calculating normalized content hashes;
- reports line/word geometry, line count, and a content hash;
- runs asynchronously on the subtitle worker;
- is throttled when the existing fast bar signature shows unchanged caption
  content.

Today OCR is used for classification and geometry only. VP copies original
subtitle pixels; it does not retain recognized text or render new glyphs.

## Scope

Add a new `SubtitleRepositionMode::BAR_OCR` mode for the writable DirectShow
live-source P010 path. It is opt-in and disabled by default.

`BAR_OCR` is intentionally conservative:

```text
confirmed encoded black bar
AND every recognized word box, plus safety margin, lies entirely inside that bar
→ erase source caption and draw cached replacement caption

any word box touches or crosses into active picture
→ do nothing; preserve the original source pixels
```

For example, a two-line caption entirely in the lower bar is eligible. A
speaker label or Netflix-style caption touching the picture boundary is not.

## Non-goals

- Do not alter existing BASIC or ADVANCED subtitle modes.
- Do not move, erase, OCR, or redraw any caption that intersects the active
  picture rectangle, even partly.
- Do not process arbitrary screen text, OSDs, logos, credits, or text in the
  visible picture.
- Do not implement alpha/libplacebo support in this story. The alpha renderer
  has no writable DirectShow live-source sample path; it requires a separate
  renderer-native compositing story.
- Do not use the GPU OCR/DirectML model for this strict mode initially. Windows
  OCR is sufficient because it runs only at cue changes.

## Configuration and UI

1. Extend `SubtitleRepositionMode` and command/config parsing to accept
   `bar_ocr` (case-insensitive), while preserving `false`, `basic`, and
   `advanced` behavior.
2. Document the mode in `VideoProcessor.cfg` and `CONFIGURATION.html`:
   `subtitle_reposition=bar_ocr`.
3. Add tightly scoped settings with safe defaults, preferably under a dedicated
   subtitle section rather than adding unrelated command-line switches:
   - replacement text scale relative to recognized font height;
   - horizontal panel padding;
   - vertical panel padding;
   - strict bar-boundary safety margin;
   - maximum replacement-panel height as a fraction of active picture height.
4. Keep the existing UI path that calls `SetSubtitleRepositioningMode`; the new
   mode must report its active/unavailable status truthfully in logs/OSD where
   current subtitle state is exposed.

## Implementation plan

1. Extend `WindowsOcrSubtitleResult` and the internal subtitle analysis result
   to retain normalized UTF-16 cue text and explicit line breaks, not merely a
   content hash and word boxes. Preserve speaker labels and punctuation.
2. Require stable encoded bar geometry before accepting a cue. Restrict OCR
   input to the lower-bar search region for this mode; do not scan text inside
   the picture.
3. Implement strict eligibility:
   - group recognized words into their OCR lines;
   - reject if no credible centered subtitle lines remain;
   - reject if any accepted word/line rectangle, expanded by the configured
     margin, reaches `pictureBottom` or lies outside the lower bar;
   - reject ambiguous OCR, unavailable OCR, empty text, or an oversized panel;
   - on any rejection, make no source-frame modification.
4. Create a cached `SubtitleCue` containing normalized text, line breaks,
   content hash, source bounds, stable picture/bar geometry, text layout, and a
   rasterized glyph mask. Cache only after the strict gate passes.
5. Use DirectWrite (and font fallback) to rasterize the recognized cue into a
   grayscale/luma glyph mask. Render a centered opaque black panel of the
   configured width with the replacement text larger than its source text.
   Preserve multiple lines; do not merge them into one line. Cap panel height
   and fall back to untouched source when the cue cannot fit safely.
6. Per output frame, perform only inexpensive work while the cue signature and
   geometry remain unchanged:
   - clear the source caption area, or the complete confirmed lower black bar;
   - composite the cached black panel and cached glyph mask at the selected
     destination in the visible picture;
   - write neutral chroma for the generated panel/glyph region.
   Do not run OCR, DirectWrite layout, font rasterization, or full glyph
   extraction for unchanged frames.
7. Reuse and strengthen the existing lower-bar fast signature to decide when to
   schedule OCR. A changed signature invalidates the cached cue; use a short
   debounce/confirmation to avoid rerendering during subtitle fades.
8. Maintain latest-frame-only worker behavior. OCR can never queue unbounded
   work or block the DirectShow delivery thread.
9. Reset the cached cue, signatures, and generated masks on renderer restart,
   stream reset, EOTF/format change, bar-geometry change, mode change, OCR
   failure, and caption disappearance.
10. Log only meaningful events: cue accepted (hash, lines, geometry), cue
    rejected (reason), cached cue invalidated, OCR availability failure, and
    periodic replacement counts. Never log recognized subtitle text by default.

## Verification

- Add unit tests for strict eligibility using synthetic lower-bar geometry:
  - one-line and two-line captions wholly inside the lower bar are accepted;
  - a line touching/crossing picture boundary is rejected without modifying the
    source;
  - off-center UI-like text and oversized panels are rejected;
  - line breaks and speaker labels are retained in the cached cue;
  - a changed content hash invalidates only the old cue.
- Add tests for cue-cache behavior proving unchanged frames do not invoke OCR or
  DirectWrite layout/rasterization again.
- Test real one-line and multi-line Apple TV captions in lower bars. Validate
  no original glyph/black-panel remnants remain and that the replacement panel
  stays stable through repeated frames.
- Test Netflix-style or other captions overlapping active picture. Confirm VP
  leaves every source pixel unchanged.
- Test 23.976, 24, 50, 59.94, and 60 Hz playback, renderer restarts, scene
  changes, HDR/SDR transitions, and OSD on/off. Subtitle mode must not drain
  queues, cause dropped frames, or introduce accumulating latency.

## Acceptance criteria

- BAR_OCR replaces only captions wholly inside a confirmed lower encoded black
  bar; boundary-touching captions remain untouched.
- Multi-line captions are redrawn as multi-line captions and fit in a bounded,
  centered black panel over the visible picture.
- OCR, layout, and glyph rasterization occur only on a new/changed cue, not on
  repeated frames.
- Existing BASIC and ADVANCED behavior is unchanged.
- The story is not Done until build/test evidence and real-source user
  validation are recorded in this Status section.
