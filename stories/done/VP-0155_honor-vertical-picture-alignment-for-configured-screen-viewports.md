# VP-0155: Honor vertical picture alignment for configured screen viewports

## Status

Done — accepted, deployed for testing, and merged on 2026-08-27.

The fix was implemented on `codex/fix-vertical-picture-alignment` from the
current `v1.3.001-beta` integration line. After rebasing onto the then-current
tip `e0d302e`, source commit `33fa8e9` was merged by
[PR #70](https://github.com/billslack2/videoprocessor/pull/70) into
`v1.3.001-beta` as merge commit `2d7f9eb`.

This record was created directly in `done` after the developer exercised the
deployed fix, confirmed that all three alignment choices work, and requested
merge and retrospective tracking.

## User story

As a VideoProcessor operator using a configured scope screen viewport, I want
Top, Center, and Bottom picture alignment to move the complete fitted
presentation to the selected vertical resting position, including when NLS is
active, so the setting visibly matches the configuration UI and supports
one-sided masking.

## Root cause and implementation

- The selected `vertical_alignment` value was parsed and propagated, but the
  configured screen viewport's destination fit was hard-coded to `CENTER`.
- Active NLS returns after that first fit, so it never reached the later linear
  picture fit that honored the selected alignment.
- The renderer now resolves and applies the selected vertical alignment during
  the configured-screen fit. The later linear fit continues to use the same
  setting, preserving top/center/bottom behavior without changing source crop,
  scale, aspect, or active-picture authority.
- Deterministic geometry coverage verifies a 2.35:1 configured screen inside a
  16:9 output for all three resting positions.

## Acceptance criteria

1. Top, Center, and Bottom affect the configured screen viewport whenever
   unused vertical output space exists.
2. Active NLS honors the selected resting alignment instead of always
   centering at its terminal screen-fit stage.
3. Center remains backward compatible.
4. Alignment changes destination placement only; source crop, scale, aspect,
   and active-picture authority remain unchanged.
5. The deployed x64 Release binary pair works under live user validation.

## Validation and deployment evidence

- Focused post-rebase native alignment tests passed 2/2.
- The complete native suite passed 878/878 on the original fix commit before
  the one-commit beta-tip rebase; the intervening beta commit was unrelated to
  renderer geometry.
- Clean x64 Release rebuilds passed for both the VideoProcessor GUI and the VP
  Renderer after rebasing onto `e0d302e`.
- A matching x64 Release executable/renderer pair from the code-identical
  pre-rebase fix was deployed to `C:\Videoprocessor\vp`; deployed hashes were
  verified against the build artifacts and configuration was untouched.
- The previous deployed pair was backed up under
  `C:\Videoprocessor\vp\backups\vertical-alignment-20260827-101201`.
- The developer tested the deployment and confirmed the behavior works.

## Files

- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- `src/VideoProcessor-Test/AlphaSourceCropPolicyTests.cpp`
