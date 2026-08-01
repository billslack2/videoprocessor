# VP-0070: Alpha panel-bound subtitle capture and relocation without OCR

## Status

Backlog. The objective is decomposed into ordered child tasks. The active
implementation is [VP-0070-1](../in-progress/VP-0070-1_panel-glyph-detector-and-contract.md).
The root closes only after all children are done and the full panel-treatment
path has representative live-capture evidence.

## User story

As an Alpha renderer user whose Apple TV captions are guaranteed to appear on
an opaque, visually uniform, dark-ish panel, I want VP to capture stable glyph
and panel geometry, remove the source panel, and render the captured glyphs in
a stable destination panel without OCR, so I never see the original subtitle
flash before its fixed or moved form.

## Input contract

This feature is opt-in and applies only while all of these are true:

- each source caption is on an opaque, low-variance dark panel (black,
  charcoal, or gray are valid);
- the glyphs have sufficient luma or color contrast with the panel;
- the panel lies in the configured subtitle band; and
- captions do not occur outside such a panel.

If the contract cannot be established for a frame, Alpha must pass it through
unchanged and report `unavailable`. It must not attempt in-picture detection,
OCR, neural text recognition, or speculative pixel modification.

## Objective

Implement a deterministic, non-OCR Alpha pipeline that:

1. finds the dark panel before using glyph geometry;
2. estimates its stable background color and extracts a tight, soft glyph mask
   from the contrast with that color;
3. freezes the panel, glyph, mask, and destination geometry for a cue; and
4. restores the source panel to its learned solid color and composites the
   captured visual glyphs onto a destination panel in the same treated frame.

No recognized text, dictionary, language model, OCR API, ONNX runtime, or
neural detector belongs in this story.

## Temporal and presentation rules

- The first plausible panel may be treated on that frame by the bounded
  deterministic prefilter; no worker result may be awaited before present.
- A cue becomes `stable` after the configured matching observations. Its
  panel rectangle, glyph rectangle, background color, soft mask, and
  destination rectangle then remain immutable until cue loss or a confirmed
  cue transition.
- Per-frame validation decides only whether the same cue persists. It must not
  chase a subtitle or jitter either rectangle.
- Source restoration and destination compositing are atomic for a treated
  frame: VP must never show the source glyphs and then the moved form.
- Geometry is tied to the exact source frame, raster, renderer/video-state,
  active-picture, and viewport generations. Any mismatch invalidates treatment
  rather than applying stale geometry.

## Decomposition

1. [VP-0070-1](../in-progress/VP-0070-1_panel-glyph-detector-and-contract.md)
   — create the renderer-neutral dark-panel/glyph detector, immutable cue
   contract, and synthetic unit tests.
2. [VP-0070-2](VP-0070-2_alpha-panel-diagnostic-overlay.md) — integrate the
   contract into Alpha and render opt-in stable panel/glyph diagnostics in the
   current visible coordinate system.
3. [VP-0070-3](VP-0070-3_same-frame-panel-restoration-and-glyph-relocation.md)
   — restore the source panel and composite captured glyphs into a stable
   destination panel without an original-subtitle flash.
4. [VP-0070-4](VP-0070-4_panel-subtitle-live-validation-and-performance.md)
   — validate real Apple TV captures, stability, failure behavior, and the
   VP-0066 low-latency evidence.

Each later child depends on acceptance of the previous one. The root is done
only when all four are done and the input contract has been demonstrated on
representative live captures.

## Validation requirements

Use SDR, HDR, and LLDV-derived Apple TV captures at 23.976, 24, 50, 59.94, and
60 Hz. Include black, charcoal, and gray panels; bright, dim, outlined, and
multi-line glyphs; cue changes; dark non-caption material in the subtitle band;
and normal plus scope/CIH profiles. Retain captures, panel/glyph/destination
geometry, cue state, source generations, and CPU/GPU/present evidence.

## Root acceptance criteria

- No OCR, text recognition, neural detector, or in-picture subtitle path is
  introduced.
- Stable cues retain identical panel, glyph, and destination geometry for
  their complete lifetime.
- A treated frame contains either the original untouched input or the restored
  source panel plus destination glyphs, never both versions.
- Missing or ambiguous panel evidence fails safe to unchanged output.
- VP-0066 queue, liveness, and latency evidence shows no new unbounded queue,
  blocking readback, sustained frame drop, or presentation regression.

