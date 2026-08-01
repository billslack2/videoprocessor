# VP-0070-1: Bar/boundary CueSet architecture and detector benchmark

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
The product scope is narrower: a qualified line's meaningful glyph ink must
cross a stable top/bottom active-picture boundary into an encoded bar or lie
wholly inside one encoded bar. The original full-picture Sintel corpus cannot
prove that condition and is retained only for cadence and multi-panel
regression evidence.

The corrected boundary/bar glyph-first fixture established a viable starting
contract without pretending to recover invisible source-panel endpoints:

- all 672/672 full-video top/bottom straddle, top-bar-only, and bottom-bar-only
  eligible frames accepted;
- 0/581 picture-only, near-boundary menu, and no-caption frames accepted;
- complete eligible synthetic glyph coverage after full-line grouping;
- deterministic derived capture rectangles had zero coordinate drift across
  stable CueSets; and
- PP-OCRv3 added no recall or rejection benefit to the glyph-first gate while
  adding about 13 ms median CPU inference cost.

Review exposed that the original frame-level metric hid an incomplete top
line: the short `TOP` word was discarded before baseline grouping. The
corrected detector keeps short word/speaker components, groups words using a
glyph-height gap bound, and measures complete eligible glyph coverage. Real
compressed P010 soft-mask quality remains unproven.

Experiment and results are under
`C:\Users\bslac\vp\test-media\vp-0070\experiments`.
Worktree: `C:\\Users\\bslac\\vp\\worktrees\\vp-0070-1-panel-detection`.
The branch must be rebased onto the latest local VP-0066 commit before every
implementation iteration; the current observed tip is `9de490a`.

Prior synthetic unit tests and build success are retained only as regression
history; they did not establish correctness on representative video.

The rebuilt implementation checkpoint is `9f65379` (rebased on VP-0066
`9de490a`). `87eda50` live logging from the
`54f0e0f` worktree build proved that Highlight mode and stable active bounds
were both correct (`0,276-3840,1884`), but the glyph seed used an SDR-style
P010 floor of 620 and never formed a proposal for visibly white PQ captions.
`87eda50` replaces that assumption with a bar-relative path: low-code seeds
are eligible only on the encoded-bar side of trusted bounds, must have strong
dark support in two directions, and are accepted/masked only at a measured
minimum contrast above the learned bar luma. Active-picture pixels retain the
strict floor. A 3840x2160 PQ diffuse-white (code 510) bar-caption fixture now
locks and produces a mask; x64 Release passed 413/413 tests. This remains a
testable correction, not live-validation or deployment authorization.

The latest live recording (`C:\Users\bslac\Videos\2026-08-01 17-18-48.mp4`)
also showed that treating individual lines as individual panels produced
misleading double boxes and discarded picture-side subtitle lines. `9f65379`
uses encoded-bar overlap only as the eligibility proof: a qualifying
bar/boundary line anchors one immutable public caption rectangle, which unions
all qualifying lines plus a nearby horizontally aligned picture-side line.
Highlight paints the glyph masks but one outer capture rectangle and one union
glyph rectangle. The regression fixture covers one line crossing the bottom
bar plus a second line wholly in picture. Move-mode composition still consumes
the individual mask anchors and therefore needs its own single-operation
conversion before it may be considered production-ready.

The rebuilt implementation otherwise uses bounded
top/bottom bar and boundary acquisition, separate geometry for up to three
lines, dark-backing and optional neutral-chroma qualification, soft glyph
masks, and stable ROI-only validation. Idle no-cue acquisition runs every
third frame and UHD acquisition samples at most roughly 1920x1080 seed
locations; a pending cue or failed stable validation bypasses that cadence.
Changed content at the same bounds releases to `candidate` before it may lock
again, while stable cue geometry stays frozen for cues of arbitrary duration.
No OCR or neural model is required or invoked by this path.

It also makes symmetric bar acquisition robust to a bounded subtitle band:
boundary search can look through the bright interruption, while four of six
distributed dark/neutral bar-depth slices plus inner-boundary contrast remain
required for crop authority. One-sided top-only or bottom-only bar rectangles
are accepted by the subtitle detector when a caller supplies stable authority;
global active-picture crop policy remains conservative.

Build evidence: clean x64 Release succeeded at `9f65379`, with 416/416 tests
passing. Representative compressed P010 mask quality and live false-treatment
evidence remain open, so this task stays in backlog and the build is not yet a
deployment authorization.

The 2026-08-01 expert-integration checkpoint is `ba6a9d7`, still based on the
latest local VP-0066 tip `9de490a`. It replaces the interim public union with an
explicit `PanelSubtitleCueSet`: every member is contrast-qualified, represented
in the current soft mask, and admitted only when it is a related line on the
same top/bottom side. A picture-side companion can join only through the
bar/boundary anchor's backing evidence. Highlight exposes one cue capture box;
diagnostic Move captures all member glyphs first, erases their combined source
mask, and relocates the preserved multiline layout in one operation. Capture
padding is based on the largest member height and bar-only captures are clamped
to the trusted bar, avoiding the prior combined-height expansion.

Stable cue identity now tolerates a two-frame detection dropout, but the held
result is marked `currentMaskVerified=false`; Highlight and Move fail closed and
cannot mutate a frame using stale pixels. A VP-0070-local active-picture
authority retains a proven crop through ambiguous/dark full-raster
publications, switches to a different crop after three strong confirmations,
and releases a crop only after 12 consecutive explicitly trusted full-raster
observations. Pipeline, mode, raster, and active-picture reset generations
invalidate it immediately; VP-0066 global publication behavior is unchanged.

`d8a691a` also adds the renderer-neutral asynchronous glyph-segmentation
provider contract needed for the proposed GPU-region/compact-neural-mask path:
normalized P010 ROI metadata, exact source and generation tokens, per-member
interior/edge soft masks, telemetry, an unavailable default, and latest-only
stale-result rejection. This is an interface and safety boundary only—no model,
weights, inference runtime, training data, OCR, or production neural path is
present. A clean x64 Release build at `ba6a9d7` passed 424/424 tests. Deployment
remains paused; representative live-video mask/false-treatment validation and
the actual segmentation provider remain open.

## Parent

[VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

Prove and implement a renderer-neutral bounded `SubtitleCueSet` containing
separate line members but one actionable cue geometry. Each member has
immutable backing evidence, glyph, tight mask, and capture geometry tied to
exact frame/raster/pipeline/picture/viewport generations. Classical pixels
remain authoritative until a generation-safe segmentation provider returns a
current result. The existing Apache-2.0 PP-OCRv3 remains an optional benchmark
provider only; it is off by default and is not required by the architecture
unless new real-capture evidence proves a measurable benefit.

## Design constraints

- Require stable active-picture geometry and scan only bounded strips centered
  on actual top/bottom encoded-bar boundaries.
- Apply a pure eligibility predicate after local backing/mask extraction:
  meaningful glyph-mask pixels with dark backing must cross exactly one
  boundary or lie wholly inside one encoded bar.
- Derive the new capture/destination box from frozen tight glyph geometry plus
  deterministic padding. Do not report inferred padding as an observed source
  panel endpoint.
- Accept qualified top- and bottom-bar-only lines. Reject picture-only,
  padding-only, whole-frame, and ambiguous top-plus-bottom candidates.
- Represent up to three independently validated line members in one CueSet.
  Preserve their relative layout, but expose one capture/inpaint/move operation
  so overlapping lines cannot be erased or relocated independently.
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
- The deterministic fixture accepts every labelled top/bottom crossing and
  bar-only line and rejects every labelled picture-only, menu, and clean frame
  before production work begins.
- Unit tests establish detection on synthetic black, charcoal, and gray panels
  with bright, dim, outlined, and two-line glyph patterns.
- Tests reject panels outside the bounded top/bottom boundary strips,
  low-contrast content, and non-uniform dark picture content.
- Tests accept meaningful top/bottom glyph crossings and captions wholly inside
  either bar, and reject the same panels moved wholly into picture.
- Tests reject a panel crossing whose glyph ink remains on only one side.
- Tests reject the supplied dark-suit capture and black title/credit interval.
- Tests prove stable cue geometry is byte-for-byte unchanged across repeated
  matching frames, and that a changed fingerprint releases/reacquires rather
  than jittering geometry.
- Tests prove generation/raster mismatch produces no applicable stable result.
- Soft-mask refinement recovers antialiased glyph support sufficiently for
  source inpaint on representative compressed P010 captures.
- The x64 Release test build passes from the recorded VP-0066 baseline.

## Out of scope

Production neural inference/model assets, full recognition, production source
restoration/relocation, and deployment. DirectShow Highlight/Move remain
diagnostic test modes until representative live-video evidence satisfies the
mask-quality and false-treatment criteria.
