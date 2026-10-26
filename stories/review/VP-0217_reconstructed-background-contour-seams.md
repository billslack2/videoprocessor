# VP-0217 - Improve reconstructed subtitle background color and contour seams

## Status
Review

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
## Review evidence - 2026-10-26

Implementation saved and pushed at `bd01f79c4aff6aa554de132c4525693e46452148`.
The final complete x64 Release solution build passed from clean source; generated
host and renderer version headers identify bd01f79 with VERSION_DIRTY=false.
Initial Windows PowerShell module-path failure was environmental; the successful
build explicitly uses Windows PowerShell's standard module directories. Official
runtime hash sidecars were produced by the normal build target.

All **92 selected native/actual-D3D11 GPU tests passed**, including cut/paste
geometry, donor exclusion, alpha/split composition, transparent yellow, and
linear/sRGB/PQ/HLG coverage. The targeted Config surrounding-fill save/reopen
round-trip also passed. The first broad GPU run was stopped after shader sampling
was refactored to keep HDR decoding at one compiled site; final results above
are from that optimized implementation. No full repository suite is claimed.

Synthetic sloping-color ground truth: mean absolute error 0.015620 for the new
surround fill, versus 0.050365 bottom / 0.048600 top for Mirror. The two-row
boundary error was 0.002780. This tests smooth contour continuation, not semantic
recovery of the supplied screenshot or arbitrary natural textures.
Warmed 3840x2160 full preparation with a 1600x130 removal region measured
0.552810 ms in the focused run and 1.107895 ms in the regression run. Mirror
measured 0.940810 and 0.513400 ms respectively. Timings are bounded local wall-time
measurements, not whole-player latency or a claimed speedup over prior surround.

Trial: `E:\codex\reconstruction-seams-trial-20261026`.
`TRY-NEW-BLEND.cmd` uses the copied Clear / Yellow profile with only cleanup_fill
changed from stretch to surround. `COMPARE-STRETCH.cmd` uses the copied original
config. Each config has its own copied state; both run the same clean Release
runtime pair. Blur, opacity, text color and other original settings are preserved.
Configuration backup: `backups\VideoProcessor.cfg.before-trial`. Every manifest
runtime copy hash matched its source. The main deployment was not modified or
restarted. No merge or public release was made.

Remaining acceptance: user comparison on the actual scene and moving video,
particularly seam visibility, contour distortion and temporal shimmer. The
feature remains Review pending that visual assessment.
Evidence: `assets/VP-0217/reconstruction-seams-20261026/`.