# VP-0070-1: Boundary-crossing CueSet architecture and detector benchmark

## Status

Backlog. The implementation in `a3ecbd5`/`c6e251b`/`ccbc240` on
`codex/vp-0070-1-panel-detection` failed live validation and is rejected as the
production basis. It must not be redeployed. Its single widest-dark-run panel,
exact-pixel fingerprint, full-raster mask, and candidate painting are replaced
by this task.

The 2026-08-01 reproducible Sintel spike established:

- loose classical panel/glyph evidence: 97.2% sampled caption-frame recall but
  15.6% no-caption false positives on end-title text;
- PP-OCRv3 detection-only: 97.2% proposal recall and the same 15.6% false
  positives, proving that an off-the-shelf text detector is not a subtitle
  classifier;
- strict observable-panel boundary plus PP-OCR proposal: zero false positives
  but only 27.8% recall because black panels can visually merge with a dark
  picture/bar; and
- an explicit lower-quarter Apple TV profile: 97.2% recall and zero false
  positives on this synthetic corpus, but it is a style prior, not a general
  solution.

Those results motivated the architecture but no longer define eligibility.
The product scope is narrower: a qualified line's panel and meaningful glyph
ink must cross a stable top or bottom active-picture boundary into an encoded
bar. The original full-picture Sintel corpus cannot prove that condition and is
retained only for cadence and multi-panel regression evidence.

The boundary-specific glyph-first fixture then established a viable starting
contract without pretending to recover invisible source-panel endpoints:

- 28/28 sampled top/bottom straddle positives accepted;
- 0/77 picture-only, bar-only, near-boundary menu, and no-caption negatives
  accepted;
- deterministic derived capture rectangles had zero coordinate drift across
  repeated samples; and
- PP-OCRv3 added no recall or rejection benefit to the glyph-first gate while
  adding about 13 ms median CPU inference cost.

The high-confidence glyph seed mask covered only 30.3% of the pair-derived
antialiased glyph truth. Detection/identity is therefore a starting point, not
yet an inpaint-ready mask. Full soft-mask refinement remains required before
production implementation.

Experiment and results are under
`C:\Users\bslac\vp\test-media\vp-0070\experiments`.
Worktree: `C:\\Users\\bslac\\vp\\worktrees\\vp-0070-1-panel-detection`.
The branch must be rebased onto the latest local VP-0066 commit before every
implementation iteration; the current observed tip is `972c119`.

Prior synthetic unit tests and build success are retained only as regression
history; they did not establish correctness on representative video.

## Parent

[VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

Prove and implement a renderer-neutral bounded `SubtitleCueSet` containing
separate line members. Each member has immutable backing evidence, glyph, tight mask, and
capture geometry tied to exact frame/raster/pipeline/picture/viewport
generations. Classical pixels remain authoritative. The existing Apache-2.0
PP-OCRv3 remains an optional benchmark provider only; it is off by default and
is not required by the architecture unless new real-capture evidence proves a
measurable benefit.

## Design constraints

- Require stable active-picture geometry and scan only bounded strips centered
  on actual top/bottom encoded-bar boundaries.
- Apply a pure boundary-eligibility predicate after local backing/mask
  extraction: meaningful glyph-mask pixels with dark backing must exist beyond
  an antialiasing margin on both sides of exactly one boundary.
- Derive the new capture/destination box from frozen tight glyph geometry plus
  deterministic padding. Do not report inferred padding as an observed source
  panel endpoint.
- Reject picture-only, bar-only, padding-only, one-row antialiasing, whole-frame,
  and ambiguous top-plus-bottom candidates.
- Represent up to three independently boxed line panels without merging their
  actionable capture/inpaint geometry.
- All returned rectangles are half-open source-raster coordinates.
- The contract includes source-frame/raster and all caller-provided generation
  tokens, panel and glyph rectangles, panel color, soft mask bounds,
  fingerprint, confidence/state, and detector cost.
- A stable cue freezes its geometry and color; tolerant 64x16 ternary/census
  signatures validate it without exact pixel equality or geometry smoothing.
- Neural/text proposals run in a one-slot latest-frame worker only on
  acquisition/change/sparse heartbeat. Stale or mismatched results are rejected.
- Buffer sizes and work are bounded. The implementation performs no allocation
  on its steady-state validation path.

## Acceptance criteria

- The corpus benchmark reports classical-only, PP-OCR proposal-only, and
  combined precision/recall/false-positive and P50/P95/P99 cost.
- The deterministic boundary fixture accepts every labelled top/bottom
  crossing and rejects every labelled picture-only, bar-only, menu, and clean
  frame before production work begins.
- Unit tests establish detection on synthetic black, charcoal, and gray panels
  with bright, dim, outlined, and two-line glyph patterns.
- Tests reject panels outside the bounded top/bottom boundary strips,
  low-contrast content, and non-uniform dark picture content.
- Tests accept meaningful top and bottom glyph crossings and reject the same
  panels moved wholly into picture or bar.
- Tests reject a panel crossing whose glyph ink remains on only one side.
- Tests reject the supplied dark-suit capture and black title/credit interval.
- Tests prove stable cue geometry is byte-for-byte unchanged across repeated
  matching frames, and that a changed fingerprint releases/reacquires rather
  than jittering geometry.
- Tests prove generation/raster mismatch produces no applicable stable result.
- Soft-mask refinement recovers antialiased glyph support sufficiently for
  source inpaint; the current 30.3% seed-mask recall is not acceptable.
- The x64 Release test build passes from the recorded VP-0066 baseline.

## Out of scope

Alpha/DirectShow integration, overlay rendering, source restoration, glyph
relocation, full recognition, and any build or deployment.
