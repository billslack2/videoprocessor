# VP-0162: Match profile-status OSD top padding to its horizontal padding

## Status

Review (2026-08-28). The tracker was synchronized with `origin/main` before assignment. Implementation retained the deployed local integration, whose history begins at remote beta `v1.3.003-beta` (`0cf07a95`), preserving VP-0147 and VP-0161 functionality.

Source commit `9c09304` adds a visible-picture-scaled top inset matching the additional left inset. The focused x64 Release placement suite passed 11/11, and the complete x64 Release solution build completed with `VERSION_DIRTY=false`.

The matched Release pair was deployed to `C:\Videoprocessor\vp` after both VideoProcessor and VideoProcessorConfig were stopped. Backup: `C:\Videoprocessor\vp\backup-before-vp0162-20260828-195917`.

- `VideoProcessor.exe` SHA-256: `40D095500D952508CD6519CBEBDE1F3F0960983EEA8D26FB1A69CCEEA8C838B5`.
- `vprenderer\VideoProcessorVPRenderer.dll` SHA-256: `76BC620E76A26EF9315EA77425AC80FA22C316E1A53A86C899B0775458A80A2E`.

Both deployed hashes match the build artifacts. The active `VideoProcessor.cfg` was not edited and retained SHA-256 `A22081F9BDAE9681A442D19DD4CB9E6F10A7156AF166FF1B84989545BA36691B`. Both programs restarted and remained running.

## User story

As a VideoProcessor operator, I want the Alpha profile-status OSD to have the same intentional clearance from the top of the visible picture as it has from the left, so the banner looks evenly inset rather than crowded against the top edge.

## Required behavior

1. Add a top-padding allowance equivalent to the existing scaled additional left inset for the native profile-status banner.
2. Calculate it from visible-picture height, so scope and zoom placements keep the same visual relationship as the existing horizontal padding.
3. Keep containment behavior unchanged: if the picture cannot accommodate the requested inset, the banner must remain wholly visible and mark the inset as clamped.

## Acceptance criteria

1. At the 1080-pixel reference height, both the profile banner's left and top edge are inset by 60 pixels.
2. The top inset scales with the visible picture at 720p, 1440p, and 2160p.
3. Focused placement tests and an x64 Release build pass.
4. The deployed executable and renderer DLL come from one successful x64 Release build, have matching deployed/build hashes, and the active configuration remains unmodified.

## Non-goals

- Changing profile-status content, fade timing, or the Ctrl+I diagnostics OSD.
- Changing user configuration values.

The change was integrated onto the confirmed latest beta branch
`v1.3.003-beta` at commit `d07ddb9` (remote beta base `0cf07a95`). A clean
beta worktree built the focused x64 Release test target and passed the same
11/11 placement tests.
