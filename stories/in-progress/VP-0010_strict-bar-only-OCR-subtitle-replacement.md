# VP-0010: Strict opaque-panel OCR subtitle replacement

## Status

Validating.

Implementation branch:
`VP-0010`, rooted at `origin/main` and advanced through the existing subtitle
prerequisite commit `4409d48`.

Implementation commits: `70a0234`, with tight-panel and replacement-layout
corrections in `b67895c` and cue-lifetime/multiline corrections in `95e08f2`.
Panel-signature caching, three-observation cue consensus, stable cached
rendering, smaller replacement text, and continuous source/destination cleanup
are in `fb76699`. PP-OCRv6 recognition, upper-edge panels, Unicode cues,
source-height rendering, and reliable per-cue glyph fingerprinting are in
`5dec29c`. The corrected timing-test expectations were ported from the beta
work branch in `6bce8a1`. Runtime-recording corrections for stale cue
transitions and unbounded panel growth are in `97a63e0`.

Validation evidence:

- Release x64 solution build succeeded with Visual Studio 2026/MSBuild 18.7.
- Seventeen focused BAR_OCR tests passed, covering opaque panels overlapping the
  upper or lower picture edge, panels wholly in an encoded bar, bare-text
  rejection, Unicode/music-cue identity, unclipped and
  size-adaptive multiline DirectWrite rasterization, oversized-layout rejection,
  independently boxed tight multiline panels, partially exposed tight-panel
  rims, per-frame panel/glyph-presence validation, cue-text normalization,
  partial multiline readings, different-cue rejection, and selection of the
  most complete multiline OCR observation, complete thin-glyph fingerprinting,
  cue disappearance, and isolation from pixels outside the confirmed panel.
- The complete x64 Release suite passes: 28 of 28 tests.
- PP-OCRv6 recognition was validated against the supplied playback captures:
  it retained `[♪♪♪]` and correctly read both lines of the supplied upper-edge
  caption.
- The July 24 OBS recording and matching analyzer log exposed a cue retained
  through multiple scene cuts and a source panel that grew from roughly 1,950
  to 2,556 pixels wide. The sparse signature was replaced with a complete
  2x2-block glyph-mask fingerprint; unconfirmed cue panels can no longer enlarge
  the active source panel, and a different OCR observation independently
  suppresses stale output.
- Real Apple TV/source playback validation is still required before completion.

## User story

As a viewer whose source places burned-in subtitles on an opaque black panel
near the top or bottom of the frame, I want VP to recognize the cue, erase its
complete source panel, and redraw it as consistently positioned text over the
visible image. The source panel may be wholly inside an encoded letterbox bar or
may overlap the adjacent active picture.

This targets sources configured like Apple TV where captions are deliberately
placed on opaque black rectangles. It must support centered, one-line and
multi-line captions including speaker labels and line breaks. Text without a
safely identifiable opaque panel remains untouched.

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
live-source P010 path. The configuration name is retained for compatibility,
but eligibility is based on a confirmed opaque near-black subtitle panel, not
solely on encoded letterbox-bar geometry. It is opt-in and disabled by default.

`BAR_OCR` is intentionally conservative:

```text
credible centered edge-region OCR cue
AND a complete opaque near-black panel can be confirmed around every cue line
AND the panel lies in the upper or lower subtitle region
→ erase the complete confirmed panel and draw the cached replacement caption

no complete opaque panel, a translucent panel, bare/outlined text, or ambiguity
→ do nothing; preserve every original source pixel
```

For example, a two-line caption entirely in the lower encoded bar is eligible.
A caption on an opaque black panel that overlaps either the upper or lower
active-picture edge is also eligible. Bare or translucent Netflix-style text
over picture is not.

## Non-goals

- Do not alter existing BASIC or ADVANCED subtitle modes.
- Do not erase active-picture pixels unless they belong to the confirmed opaque
  near-black source subtitle panel selected for replacement.
- Do not process arbitrary screen text, OSDs, logos, credits, or text in the
  visible picture.
- Do not implement alpha/libplacebo support in this story. The alpha renderer
  has no writable DirectShow live-source sample path; it requires a separate
  renderer-native compositing story.
- Do not add a second inference runtime. PP-OCRv6 detection/recognition uses the
  ONNX Runtime and DirectML components already shipped by VideoProcessor.

## Configuration and UI

1. Extend `SubtitleRepositionMode` and command/config parsing to accept
   `bar_ocr` (case-insensitive), while preserving `false`, `basic`, and
   `advanced` behavior.
2. Document the mode in `VideoProcessor.cfg` and `CONFIGURATION.html`:
   `subtitle_reposition=bar_ocr`.
3. Add tightly scoped settings with safe defaults, preferably under a dedicated
   subtitle section rather than adding unrelated command-line switches:
   - replacement text scale relative to recognized font height (100% by
     default);
   - horizontal panel padding;
   - vertical panel padding;
   - strict source-panel safety margin;
   - maximum replacement-panel height as a fraction of active picture height.
4. Keep the existing UI path that calls `SetSubtitleRepositioningMode`; the new
   mode must report its active/unavailable status truthfully in logs/OSD where
   current subtitle state is exposed.

## Implementation plan

1. Extend `WindowsOcrSubtitleResult` and the internal subtitle analysis result
   to retain normalized UTF-16 cue text and explicit line breaks, not merely a
   content hash and word boxes. Preserve speaker labels and punctuation.
2. Require stable active-picture geometry before accepting a cue. Restrict OCR
   input to bounded upper- and lower-frame subtitle search regions that include
   the encoded bars and limited guard regions inside the adjacent active picture.
   Do not scan arbitrary screen text elsewhere in the picture.
3. Implement strict eligibility:
   - group recognized words into their OCR lines;
   - reject if no credible centered subtitle lines remain;
   - starting from the OCR line/word rectangles, detect the enclosing contiguous
     opaque near-black source panel, including antialiasing, outline, shadow,
     line gaps, and panel padding;
   - accept a panel wholly in an encoded bar or overlapping an adjacent
     active-picture edge, but only when the complete enclosing rectangle is
     confirmed near-black and belongs to the cue;
   - reject bare text, translucent panels, panels with uncertain edges, or a
     panel that expands into unrelated dark picture content;
   - reject ambiguous OCR, unavailable OCR, empty text, or an oversized panel;
   - on any rejection, make no source-frame modification.
4. Create a cached `SubtitleCue` containing normalized text, line breaks,
   content hash, complete source-panel bounds, stable picture/bar geometry, text
   layout, and a rasterized glyph mask. Cache only after the strict gate passes.
5. Use DirectWrite (and font fallback) to rasterize the recognized cue into a
   grayscale/luma glyph mask. Render a centered opaque black panel with the
   replacement text at the source line height by default.
   Preserve multiple lines; do not merge them into one line. Cap panel height
   and fall back to untouched source when the cue cannot fit safely.
6. Per output frame, perform only inexpensive work while the cue signature and
   geometry remain unchanged:
   - clear the complete confirmed source-panel rectangle to nominal black;
   - composite the cached black panel and cached glyph mask at the selected
     destination in the visible picture;
   - write neutral chroma for the generated panel/glyph region.
   Do not run OCR, DirectWrite layout, font rasterization, or full glyph
   extraction for unchanged frames.
7. Replace the coarse horizontal signature with a complete 2x2-block
   bright-glyph fingerprint over the confirmed source panel. Two consecutive
   changed fingerprints invalidate the cached cue and start a short OCR burst;
   unchanged frames reuse the cached text and rasterization.
8. Maintain latest-frame-only worker behavior. OCR can never queue unbounded
   work or block the DirectShow delivery thread.
9. Reset the cached cue, signatures, and generated masks on renderer restart,
   stream reset, EOTF/format change, bar-geometry change, mode change, OCR
   failure, and caption disappearance.
10. Log only meaningful events: cue accepted (hash, lines, geometry), cue
    rejected (reason), cached cue invalidated, OCR availability failure, and
    periodic replacement counts. Never log recognized subtitle text by default.

## Verification

- Add unit tests for strict eligibility using synthetic lower-frame geometry:
  - one-line and two-line captions wholly inside the lower bar are accepted;
  - captions on confirmed opaque panels overlapping the lower active picture
    are accepted;
  - bare text, translucent panels, and ambiguous dark picture regions are
    rejected without modifying the source;
  - the complete confirmed panel rectangle is cleared, including padding,
    shadows, antialiasing, and gaps between lines;
  - off-center UI-like text and oversized panels are rejected;
  - line breaks and speaker labels are retained in the cached cue;
  - a changed content hash invalidates only the old cue.
- Add tests for cue-cache behavior proving unchanged frames do not invoke OCR or
  DirectWrite layout/rasterization again.
- Add upper-edge and Unicode/music-symbol cases.
- Test real one-line and multi-line Apple TV captions in lower bars. Validate
  no original glyph/black-panel remnants remain and that the replacement panel
  stays stable through repeated frames.
- Test bare/translucent Netflix-style captions over active picture. Confirm VP
  leaves every source pixel unchanged.
- Test 23.976, 24, 50, 59.94, and 60 Hz playback, renderer restarts, scene
  changes, HDR/SDR transitions, and OSD on/off. Subtitle mode must not drain
  queues, cause dropped frames, or introduce accumulating latency.

## Acceptance criteria

- BAR_OCR replaces only captions enclosed by a confirmed opaque near-black
  source panel in an upper or lower subtitle region. The panel may be wholly in
  an encoded black bar or overlap the adjacent active picture.
- The complete source panel is cleared without glyph, outline, shadow, or panel
  remnants. Rejected candidates leave every source pixel unchanged.
- Multi-line and Unicode captions are redrawn without losing explicit line
  breaks or symbols and fit in a bounded, centered black panel over the visible
  picture.
- OCR, layout, and glyph rasterization occur only on a new/changed cue, not on
  repeated frames.
- Existing BASIC and ADVANCED behavior is unchanged.
- The story is not Complete until build/test evidence and real-source user
  validation are recorded in this Status section.
