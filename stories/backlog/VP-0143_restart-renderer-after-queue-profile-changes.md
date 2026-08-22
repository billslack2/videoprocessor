# VP-0143: Restart renderer after queue-profile changes

## Status

Backlog (2026-08-22). A queue-profile change must request the existing
controlled renderer restart after the new profile state has been applied.
Queue settings affect the active presentation pipeline; leaving that pipeline
running can retain behavior from the prior queue selection.

## User story

As a VideoProcessor user, I want a changed queue profile to restart the active
renderer automatically, so the newly selected queue behavior takes effect
without requiring a separate manual restart.

## Required behavior

1. When a user-initiated queue-profile selection changes the effective queue
   profile, apply the selected state and request exactly one controlled
   renderer restart through the existing safe lifecycle path.
2. Do not request a restart when the selected queue profile is already active,
   the selection is rejected, or an unrelated profile group changes.
3. Coalesce rapid successive queue changes so a pending restart applies the
   latest resolved queue selection rather than repeatedly tearing down the
   renderer.
4. Preserve the normal fullscreen, DirectShow/madVR handoff, persisted-profile,
   and renderer-readiness behavior across the restart.
5. Emit a diagnostic that identifies the queue profile, change source, and
   restart outcome; surface a clear actionable failure if the restart cannot
   be completed.

## Acceptance criteria

1. Automated coverage proves that a real queue-profile change requests one
   restart only after the new profile state commits.
2. Coverage proves that reselecting the active queue profile and changing a
   non-queue profile do not request a restart.
3. Rapid queue selections are deterministically coalesced and leave the final
   selected queue profile active after the restart.
4. DirectShow/madVR and VP Renderer paths retain their respective safe
   lifecycle and readiness behavior; no stale prior queue policy survives.
5. A manual fullscreen test changes each configured queue profile and confirms
   the restart diagnostic, continued playback, and the expected effective
   queue behavior.

## Boundaries and related work

- This is a runtime renderer/graph restart, not an application process exit
  and relaunch.
- Do not change the meaning of the queue presets, external legacy batch-state
  markers, or the general persistence policy as part of this story.
- Coordinate with VP-0079 (canonical queue profiles and hotkeys), VP-0043
  (madVR graph re-prime), and VP-0141 (live renderer-settings application).

## Likely implementation areas

- `src/VideoProcessor-Lib/UnifiedProfileRuntime.cpp`
- `src/VideoProcessor-GUI/VideoProcessorDlg.cpp`
- DirectShow renderer lifecycle/re-prime integration and profile-runtime tests
