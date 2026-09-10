# VP-0180: Remove standalone stats OSD window

## Status

Review (2026-09-10). Created in Backlog, then promoted to Review at the
user's request. Implementation and x64 Release build are complete. Merged into `v1.3.005-beta` via PR #86; deployment/live playback validation
remain pending. Retain Review until that validation is accepted.

## User story

As a VideoProcessor user, I want one stats OSD composited through the madVR
or VP renderer API, without a second standalone topmost desktop overlay.

## Requirements

- Remove the stats overlay HWND, painting, visibility and positioning paths.
- Keep existing native stats bitmap generation and renderer API submission.
- Preserve requested visibility through renderer handoff; wait for native
  support instead of displaying a separate fallback window.
- Native OSD failure or unsupported renderers must not create a desktop OSD.
- Preserve native profile-change and output-sweep bitmap paths.

## Implementation and validation

- Source commit: `5aa445b6` on `codex/remove-legacy-osd`.
- Worktree: `E:\codex\videoprocessor\remove-legacy-osd`.
- Discovered current beta/default: `v1.3.005-beta`, tip `89d55ca5`.
  The user's standing instructions authorize the remote beta baseline.
- Removed window creation/registration, painting, topmost positioning and
  fallback calls; retained bitmap generation with lazy font initialization.
- Updated madVR API failure diagnostics to reflect native OSD being disabled.
- Final x64 Release build passed for VideoProcessor-GUI and
  VideoProcessor-VPRenderer; git diff --check passed.
- Build log: `E:\codex\videoprocessor\remove-legacy-osd\build-release.log`.
- No deployment or live playback validation performed.

## Acceptance and remaining validation

- On madVR and VP renderer, toggle stats in windowed/fullscreen playback and
  confirm exactly one in-frame stats panel and no separate desktop panel.
- Change renderer with stats requested and confirm native stats return after
  handoff without a stale desktop panel.
- Confirm profile-change and output-sweep overlays still render normally.
- Review removal of fallback behavior for renderers without native support.

## Tracker audit

Before allocation: 198 canonical files and 198 index rows; maximum root 0179;
registry count/next ID agree, with no duplicate IDs or missing records.
Existing VP-0088 status and VP-0179 status/index use 'In progress' instead of
canonical 'In Progress'; left unchanged as unrelated records.

## Merge evidence

- PR: https://github.com/billslack2/videoprocessor/pull/86
- Merged 2026-09-10 at 15:03:04 UTC into `v1.3.005-beta`.
- Merge commit: `6f8e6a3a023e2d3199a04ebf4ac7a9a27ba0309d`.
- Implementation head: `5aa445b6274233e647687e8e6531cf7035df7738`.
- GitHub reported clean/mergeable with no check runs attached; local x64
  Release build evidence is recorded above.
- Story intentionally remains Review per user request, pending live validation.
