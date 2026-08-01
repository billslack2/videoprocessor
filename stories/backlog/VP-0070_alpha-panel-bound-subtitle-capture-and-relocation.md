# VP-0070: Stable panel-bound subtitle capture and relocation

## Status

Backlog. The first diagnostic implementation failed live validation and must
not be deployed again: it missed real compact Apple TV panels and classified a
large dark picture region as a panel/glyph mask. VP-0070-1 and VP-0070-2 have
returned to backlog for replacement behind a multi-panel CueSet architecture.
The root closes only after the rebuilt path has representative Alpha and
DirectShow/madVR live-capture evidence.

## User story

As an Alpha or DirectShow/madVR user whose Apple TV captions appear on
an opaque, visually uniform, dark-ish panel, I want VP to capture stable glyph
and panel geometry, remove the source glyphs, and render the captured glyphs in
a stable destination panel, so I do not see the original subtitle flash before
its fixed or moved form.

## Input contract

This feature is opt-in and applies only while all of these are true:

- each source caption is on an opaque, low-variance dark panel (black,
  charcoal, or gray are valid);
- the glyphs have sufficient luma or color contrast with the panel;
- the panel lies in a deliberately configured subtitle band/profile; and
- captions do not occur outside such a panel.

If the contract cannot be established for a frame, VP passes it through
unchanged and reports `unavailable`. An optional text detector/OCR provider may
contribute asynchronous acquisition evidence, but recognized words and neural
geometry are never authoritative for panel bounds, glyph masks, capture,
inpaint, cue identity, or rendering.

## Objective

Implement a renderer-neutral panel-first pipeline for Alpha and
DirectShow/madVR that:

1. finds the dark panel before using glyph geometry;
2. estimates its stable background color and extracts a tight, soft glyph mask
   from the contrast with that color;
3. freezes the panel, glyph, mask, and destination geometry for a cue;
4. uses an optional off-the-shelf text detector only to confirm/reject a new
   text-like candidate asynchronously; and
5. restores the source glyph area to its learned panel color and composites
   the captured visual glyphs onto a destination panel in the same treated
   frame.

Recognition is allowed only if a later benchmark proves a reliability benefit
that detector-only inference cannot provide. It is never run per frame and
never controls visual geometry.

## Temporal and presentation rules

- A synchronous bounded prefilter may suppress glyphs on the first candidate
  frame only after a separately measured `safeToSuppress` gate passes. No
  worker result may be awaited before present.
- A cue becomes `stable` after the configured matching observations. Each
  member's panel rectangle, glyph rectangle, background color, soft mask, and
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

1. [VP-0070-1](VP-0070-1_panel-glyph-detector-and-contract.md)
   — replace the failed detector with a renderer-neutral multi-panel CueSet,
   configured subtitle-band policy, benchmarked classical panel/glyph
   proposals, and optional PP-OCR text-proposal evidence.
2. [VP-0070-2](VP-0070-2_always-on-panel-diagnostic-overlay.md) —
   implement temporal cue IDs, tolerant current-frame validation, immutable
   per-line geometry, and stable-only diagnostics in Alpha and
   DirectShow/madVR.
3. [VP-0070-3](VP-0070-3_same-frame-panel-restoration-and-glyph-relocation.md)
   — restore only the source glyph area and composite captured glyphs into a
   stable destination panel, including the measured first-frame policy.
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

- OCR/text detection, if enabled, is asynchronous acquisition evidence only;
  it never defines visual geometry or blocks presentation.
- A CueSet preserves each independently boxed subtitle line as a separate
  panel/glyph/capture member; a union envelope is non-actionable metadata.
- Stable cues retain identical panel, glyph, and destination geometry for
  their complete lifetime.
- A treated frame contains either the original untouched input or restored
  source glyph areas plus destination glyphs, never both versions.
- Missing or ambiguous panel evidence fails safe to unchanged output.
- VP-0066 queue, liveness, and latency evidence shows no new unbounded queue,
  blocking readback, sustained frame drop, or presentation regression.
