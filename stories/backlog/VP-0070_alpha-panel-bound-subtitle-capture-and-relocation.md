# VP-0070: CIH bar/boundary subtitle capture and relocation

## Status

Backlog. The first diagnostic implementation failed live validation and must
not be deployed again: it missed real compact Apple TV panels and classified a
large dark picture region as a panel/glyph mask. VP-0070-1 and VP-0070-2 have
returned to backlog for replacement behind a multi-panel CueSet architecture.
Deployment is explicitly paused while the boundary-only detector is proven
offline.
The root closes only after the rebuilt path has representative Alpha and
DirectShow/madVR live-capture evidence.

Build-only checkpoint (2026-08-01): `0a4cefc` on
`codex/vp-0070-1-panel-detection`, rebased through local VP-0066 tip
`09d5efa`, completed a clean x64 Release build and passed all 409 tests. The
new `subtitle_panel_test_mode` is fail-safe `off` by default and requires an
explicit `highlight` or `move` value; it is independent of the legacy OCR
`subtitle_reposition` path. No deployment or active-configuration change was
performed. Scene detection remains owned by VP-0066 and was not changed for
this story.

## User story

As an Alpha or DirectShow/madVR user with a CIH screen, I want VP to treat an
opaque-panel subtitle whose glyphs either cross into or lie wholly inside an
encoded top or bottom black bar, so off-screen captions can be recovered
without analyzing or changing ordinary subtitles entirely inside the picture.

## Input contract

This feature is opt-in and applies only while all of these are true:

- VP has a stable current active-picture rectangle and a real encoded top or
  bottom bar;
- each source caption is on an opaque, low-variance dark panel (black,
  charcoal, or gray are valid);
- the glyphs have sufficient luma or color contrast with the panel;
- locally dark opaque backing supports the line; and
- meaningful glyph-mask pixels either exist on both sides of exactly one
  active-picture boundary or lie wholly inside one encoded bar within the
  bounded search depth.

Captions entirely inside the picture, merely touching an edge through dark
backing/padding, or outside the encoded bar/boundary strips are out of scope and
must report `unavailable`. Exact original black-panel endpoints are not claimed
when they visually merge into the encoded bar or dark picture. VP instead
derives a new stable capture/destination box from the tight glyph geometry plus
deterministic padding.

If the contract cannot be established for a frame, VP passes it through
unchanged and reports `unavailable`. An optional text detector/OCR provider may
contribute asynchronous acquisition evidence, but recognized words and neural
geometry are never authoritative for panel bounds, glyph masks, capture,
inpaint, cue identity, or rendering.

## Objective

Implement a renderer-neutral panel-first pipeline for Alpha and
DirectShow/madVR that:

1. searches only bounded strips around stable encoded top/bottom picture
   boundaries;
2. qualifies locally dark opaque backing around a boundary-crossing or
   bar-contained line;
3. estimates its stable background color and extracts a tight, soft glyph mask
   from the contrast with that color;
4. requires meaningful backing-supported glyph ink to cross the same boundary
   or remain wholly inside the encoded bar;
5. freezes the panel, glyph, mask, and destination geometry for a cue;
6. uses an optional off-the-shelf text detector only to confirm/reject a new
   text-like candidate asynchronously; and
7. restores the source glyph area to its learned panel color and composites
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
- An active-picture boundary change immediately invalidates all candidates and
  stable CueSets. Old geometry is never remapped to a new boundary.

## Decomposition

1. [VP-0070-1](VP-0070-1_panel-glyph-detector-and-contract.md)
   — replace the failed detector with a renderer-neutral multi-panel CueSet,
   strict top/bottom boundary-or-bar policy, benchmarked backing/glyph
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
60 Hz. Include top- and bottom-boundary crossings and captions wholly inside
both bars; black, charcoal, and gray panels; bright, dim, outlined, and
multi-line glyphs; cue changes; picture-only captions; panel-padding-only
crossings; dark non-caption material;
and normal plus scope/CIH profiles. Retain captures, panel/glyph/destination
geometry, cue state, active-picture generations, and CPU/GPU/present evidence.

## Root acceptance criteria

- OCR/text detection, if enabled, is asynchronous acquisition evidence only;
  it never defines visual geometry or blocks presentation.
- A CueSet preserves each independently boxed subtitle line as a separate
  panel/glyph/capture member; a union envelope is non-actionable metadata.
- Eligibility requires meaningful dark-backing-supported glyph ink either on
  both sides of exactly one stable encoded-bar boundary or wholly inside one
  confirmed encoded bar.
- Stable cues retain identical panel, glyph, and destination geometry for
  their complete lifetime.
- A treated frame contains either the original untouched input or restored
  source glyph areas plus destination glyphs, never both versions.
- Missing or ambiguous panel evidence fails safe to unchanged output.
- VP-0066 queue, liveness, and latency evidence shows no new unbounded queue,
  blocking readback, sustained frame drop, or presentation regression.
