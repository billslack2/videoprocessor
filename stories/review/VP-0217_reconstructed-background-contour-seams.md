# VP-0217 - Improve reconstructed subtitle background color and contour seams

## Status
Review

Current revision: f766f1d5 adds the accepted stable blend as a separate choice.
Original surround behavior is restored; existing configurations retain their results.

2026-10-26: User approved trying color matching, edge-aware clean sample selection,
and a narrow boundary blend after showing visible joins without blur or overlays.
Implementation: `codex/reconstruction-seams-20261026`, worktree
`E:\codex\videoprocessor\reconstruction-seams-20261026`, based on freshly fetched
`origin/v1.4.02-beta` at `81ac960f`. GitHub default and latest beta agree. The
user's standing instruction authorizes this integration base.

## Scope and readiness

Add `cleanup_fill: stable_surround` while preserving original surround/mirror/stretch choices,
measured cleanup geometry, source glyph bounds, HDR transfer handling or live
configuration. Current-frame MAIN RGB samples decode to linear light before
comparison and blending. Donors must remain beyond all measured card/glyph bounds
and inside the picture. No history resource or extra frame queue is introduced.
The current implementation uses fixed normalized clean samples and
an internal boundary transition that never mixes the original subtitle back in.

## Acceptance and validation

- Actual D3D11 shader tests: unblurred color continuity and moving-frame stability
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
## Live rejection and stability revision - 2026-10-26

User rejects the first trial: substantial shimmer and inconsistent reconstruction.
Priority is consistent reconstruction and subtly wider blending. Reopen implementation
on the existing clean feature worktree; freshly verified GitHub default/latest beta
is still v1.4.02-beta at 81ac960f. Preserve the rejected trial as comparison evidence.
Remove image-dependent donor steering and color scoring, which can change sampling
locations/weights with small frame differences. Replace with fixed normalized spatial
sampling and a slightly wider internal border transition. Add actual GPU multi-frame
motion/perturbation coverage; still-image contour quality is insufficient acceptance.
## Stable revision ready for review - 2026-10-26

Saved/pushed source: `301dd70ff9dec942636bb1c4756a72642d487135` on the same feature
branch. Final complete x64 Release solution build passed with clean source identity.
No merge or deployment to the installed player was performed.

The actual GPU moving-texture/noise test reproduces the first implementation's
failure: small input changes produced maximum temporal gain 16.248779, with
0.179397 excess channel change. The revised geometry-fixed, positive normalized
sampler reports gain 0.996094 and zero excess. It has no image-dependent donor
coordinates, color scores or gradient extrapolation. It adds no temporal history
or frame latency. The internal transition grows modestly from 2-12 source pixels
to 6-20, scaled to the removal region. No original subtitle pixels are mixed in.

Validation: **85 selected native/GPU tests passed**, covering cut/paste geometry,
all surrounding-fill tests, both bars, SDR/PQ/HLG split composition, poisoned-donor
exclusion, transparent yellow cleanup, motion/noise and repeated-frame behavior.
The targeted Config save/reopen test also passed. Warmed 4K full preparation in
the final regression run averaged 0.517395 ms (Mirror 0.582870 ms); this remains
a bounded synthetic test, not live playback timing or a speedup claim.

The old still-ramp requirement of 35% improvement over Mirror was specific to
the rejected adaptive contour fitter. Fixed sampling deliberately sacrifices
that fit for temporal consistency. The replacement acceptance requires unchanged
0.025 boundary-error tolerance, lower ramp error than Mirror, and a separate
non-amplification test across moving/noisy frames. Final ramp error is 0.040881
versus Mirror 0.050365/0.048600; boundary error is 0.012541 at both bars. The initial
failure against the obsolete contour-fit threshold is retained in the evidence.

New isolated trial: `E:\codex\reconstruction-stable-trial-20261026`.
Launch `TRY-STABLE-BLEND.cmd`; `COMPARE-STRETCH.cmd` provides the original Stretch
choice using the same new binaries. The original rejected trial remains separate.
Manifest payload hashes match the clean Release build. The copied active config
changes only Clear__Yellow.cleanup_fill from stretch to surround, with its original
backup retained. No changes were made to the active installed configuration.

Remaining: user assessment of actual scene motion, seam softness and consistency.
The new regression isolates image changes at fixed accepted subtitle geometry;
it does not establish stability under arbitrary geometry changes or all scenes.
Evidence: `assets/VP-0217/stable-blend-301dd70-20261026/`.
Both new trial state files select clear__yellow for immediate comparison. Their
copied prior Classic selection is backed up in the trial's backups directory.
The installed state remains untouched.

## Separate option requested - 2026-10-26

User accepts the stable appearance but requires a new option, not replacement.
Restore original surround sampling verbatim and add Stable blend surrounding picture
(stable_surround) as a fourth choice. Existing saved surround values retain original
behavior. Fresh worktree E:\codex\videoprocessor\reconstruction-stable-option-20261026
starts from freshly fetched v1.4.02-beta at 87f61f77d3d95ddbfc1eeae343501d763d49e80b.
Source f766f1d531a336354686b9417a0a00c1df8826f0 on codex/reconstruction-stable-option-20261026.
Release build and both-option validation underway.

## Separate option ready - 2026-10-26

Original Blend surrounding picture (surround) is restored verbatim from beta.
Stable blend surrounding picture (stable_surround) is a fourth dropdown choice.
Existing saved surround configurations retain their original behavior; stable is opt-in.
Source f766f1d531a336354686b9417a0a00c1df8826f0 is committed and pushed.

Full clean x64 Release build passed. All 87 selected native/GPU tests passed,
including both modes for donor rejection, source cleanup and SDR/PQ/HLG composition.
Both Config save/reopen and retired-temporal preservation tests passed.
The motion/noise regression reports temporal gain 0.996094 and zero excess.
Warmed synthetic 4K preparation means: original surround 0.833710 ms and stable
0.909475 ms. These are bounded synthetic timings, not live playback guarantees.

Isolated trial: E:\codex\reconstruction-fill-options-trial-20261026.
TRY-STABLE-BLEND.cmd selects the new stable option; TRY-ORIGINAL-BLEND.cmd selects
original surround. Both copied states select Clear / Yellow, with prior states
and configuration backed up. Manifest runtime hashes match the Release build.
The installed player and settings remain unchanged. No merge or release performed.
Remaining acceptance: user validation of the separate choice in actual playback.
Evidence: assets/VP-0217/separate-option-f766f1d5-20261026/.