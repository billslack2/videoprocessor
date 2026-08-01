# VP-0070-1: Panel/glyph detector and immutable cue contract

## Status

Review. Implemented in `a3ecbd5` (`Add panel-bound subtitle detector
contract`) on `codex/vp-0070-1-panel-detection`.
Worktree: `C:\\Users\\bslac\\vp\\worktrees\\vp-0070-1-panel-detection`.
Baseline: latest local VP-0066 commit
`919819a` (`VP-0066 hold the steady live queue target`). The branch must be rebased onto the latest local
VP-0066 commit before every implementation iteration.

Verification: x64 Release build succeeded; all 384 `VideoProcessor-Test`
tests passed. The focused seven detector tests include a 40-second / 2,400
frame 60-fps cue sample. Production has no cue-duration threshold: a stable
cue stays frozen until its panel/glyph fingerprint or generation changes.

## Parent

[VP-0070](../backlog/VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

Create a renderer-neutral C++ value contract and deterministic CPU detector
for opaque, visually uniform dark subtitle panels and their high-contrast
glyphs. It accepts luma samples, finds a plausible panel inside a supplied
subtitle band, estimates the panel color, derives a tight glyph rectangle and
soft contrast mask, and publishes immutable cue snapshots. This child makes no
renderer, configuration, UI, source-erasure, destination-compositing, OCR,
ONNX, or neural-model changes.

## Design constraints

- Use panel uniformity and contrast to its learned background, not a fixed
  black threshold.
- All returned rectangles are half-open source-raster coordinates.
- The contract includes source-frame/raster and all caller-provided generation
  tokens, panel and glyph rectangles, panel color, soft mask bounds,
  fingerprint, confidence/state, and detector cost.
- A stable cue freezes its geometry and color; unchanged-frame validation may
  affirm or release it but cannot change it.
- Buffer sizes and work are bounded. The implementation performs no allocation
  on its steady-state validation path.

## Acceptance criteria

- Unit tests establish detection on synthetic black, charcoal, and gray panels
  with bright, dim, outlined, and two-line glyph patterns.
- Tests reject panels outside the configured band, low-contrast content, and
  non-uniform dark picture content.
- Tests prove stable cue geometry is byte-for-byte unchanged across repeated
  matching frames, and that a changed fingerprint releases/reacquires rather
  than jittering geometry.
- Tests prove generation/raster mismatch produces no applicable stable result.
- The x64 Release test build passes from the recorded VP-0066 baseline.

## Out of scope

Alpha integration, overlay rendering, source restoration, glyph relocation,
configuration/UI, live capture validation, OCR, and neural inference.
