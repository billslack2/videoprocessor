# VP-0158: Zoom-safe active-profile OSD and meaningful status filtering

## Status

Review (2026-08-28). Implemented as one contextual OSD workstream on the
`v1.3.001-beta` integration line and merged by fast-forward into
`v1.3.001-beta` at source commit `3d23a55b`.

The implementation consists of these related commits:

- `7b8dec45` keeps the native profile OSD inside the visible picture when Zoom
  cropping is active. Its left inset includes a sensible additional relative
  margin while preserving the existing top inset.
- `940b8128` adds active profile, queue, and configured NLS state to the OSD;
  adds the configurable **Show profiles** shortcut (default `Ctrl+Alt+I`);
  uses the configured profile-change hold/fade duration; and represents NLS
  enabled/disabled state graphically.
- `3d23a55b` suppresses profile categories with zero or one configured option
  and suppresses NLS status unless more than one NLS shader/mode is configured.

The work was tested together with VP-0156 and VP-0157, pushed to
`v1.3.001-beta` at `3d23a55b`, rebuilt from a fresh clean checkout of that
exact remote commit as x64 Release, and deployed to `C:\Videoprocessor\vp`.
The focused OSD tests passed 3/3, deployed artifact hashes matched the clean
build, `VideoProcessor.cfg` was unchanged, and VideoProcessor restarted
successfully.

Remaining review: operator validation of zoom-cropped OSD placement, the
complete **Show profiles** snapshot, automatic profile/queue/NLS notifications,
NLS on/off presentation, fade timing, and suppression of zero- or one-option
categories in representative live configurations.

## User story

As a VideoProcessor operator using zoomed or cropped presentation, I want the
OSD to remain fully inside the visible picture and show all meaningful active
profile, queue, and NLS choices on demand, so I can confirm the effective
presentation state without irrelevant one-option categories or clipped text.

## Context and scope

1. Anchor and scale the native OSD against the visible picture after Zoom crop,
   not against picture regions that have been cropped away.
2. Preserve the configured top inset and add a sensible relative left inset so
   the OSD is comfortably inside the visible image across resolutions.
3. Report profile changes for meaningful active categories, including queue.
4. Report which configured NLS mode is selected and show its on/off state with
   a simple graphical indicator. Never show NLS when it is not meaningfully
   configured.
5. Provide a configurable **Show profiles** shortcut that displays the complete
   meaningful active-state snapshot and uses the same configurable delayed
   fade as automatic profile-change OSD notifications.
6. Omit every profile/NLS category that has no operator choice because it has
   zero or one configured option.

## Acceptance evidence

- Native overlay-placement tests cover zoom-cropped visible-picture anchoring
  and relative inset behavior.
- Configuration and OSD tests cover the shortcut, queue and NLS reporting,
  friendly ordered labels, and single-option suppression.
- Focused final tests passed:
  `ProfileOverlayOmitsGroupsWithoutAChoice`,
  `ProfileOverlayIncludesQueueAndConfiguredNlsState`, and
  `ProfileChangeOverlayUsesFriendlyOrderedLabels`.
- A clean x64 Release build of the host, VP Renderer, Config editor, and Config
  discovery DLL completed with zero errors from remote beta commit `3d23a55b`.
- The clean build was deployed and started successfully with the active
  configuration hash unchanged.

## Related stories

- VP-0044 established native OSD visible-picture anchoring and scaling.
- VP-0152 established coordinated profile selection and on-screen feedback.
- VP-0156 was tested in the same combined build because live Screen and Zoom
  profile transitions affect the geometry used by the OSD.
- VP-0157 was included in the same combined beta deployment but is otherwise
  functionally independent.
