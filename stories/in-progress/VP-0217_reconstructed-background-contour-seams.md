# VP-0217 - Improve reconstructed subtitle background color and contour seams

## Status
In Progress

2026-10-26: User approved trying color matching, edge-aware clean sample selection,
and a narrow boundary blend after showing visible joins without blur or overlays.
Implementation: `codex/reconstruction-seams-20261026`, worktree
`E:\codex\videoprocessor\reconstruction-seams-20261026`, based on freshly fetched
`origin/v1.4.02-beta` at `81ac960f`. GitHub default and latest beta agree. The
user's standing instruction authorizes this integration base.

## Scope and readiness

Improve existing `cleanup_fill: surround` without changing mirror/stretch choices,
measured cleanup geometry, source glyph bounds, HDR transfer handling or live
configuration. Current-frame MAIN RGB samples decode to linear light before
comparison and blending. Donors must remain beyond all measured card/glyph bounds
and inside the picture. No history resource or extra frame queue is introduced.
Implementation uses bounded local contour guidance, soft donor-color scores and
an internal boundary transition that never mixes the original subtitle back in.

## Acceptance and validation

- Actual D3D11 shader tests: unblurred color continuity and sloping contour quality
  against independent undamaged synthetic ground truth, at both top and bottom bars.
- Preserve excluded bars, glyph isolation, unrelated pixels, deterministic output,
  split composition, and SDR/PQ/HLG handling through existing GPU regressions.
- Complete x64 Release solution build and appropriate native/config test coverage.
- Prepare an isolated trial from that build. User scene acceptance and real video
  performance remain required; do not claim the screenshot's hidden detail is known.
- No merge, live deployment or public release is requested by this experiment.

## Tracker audit

235 canonical files and 235 index rows; highest root 0216 and registry agree.
No duplicate IDs or file/index state mismatch found. Existing VP-0070 Status
format did not match the strict audit pattern; it is outside this change and was
left intact. Existing modified VP-0207 design.txt is unrelated and preserved.