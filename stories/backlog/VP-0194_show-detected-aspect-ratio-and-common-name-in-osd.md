# VP-0194: Show aspect ratio and name in the VP Renderer Ctrl+I OSD

## Status

Backlog. Add `Aspect ratio` and `Name` values to the existing right-side VP
Renderer Ctrl+I OSD. Use the requested labels for 4:3, 16:9, and 2.40:1; show
`Unknown` for the name of other detected ratios.

## User story

As a VideoProcessor operator, I want the VP Renderer Ctrl+I OSD on the right
side to show the detected active-picture ratio and its name, so I can quickly
understand what shape of picture VP is detecting.

## Scope and required behavior

1. Add `Aspect ratio` and `Name` values to the existing right-side Ctrl+I OSD
   when VP Renderer is active. Show them only on that VP Renderer OSD. Use the
   authoritative current detection result; do not add a new detector, crop
   policy, or aspect-ratio override.
2. Use these requested known mappings:
   - Detected `1.33:1`: display ratio `4:3`; name `TV`.
   - Detected `1.78:1`: display ratio `16:9`; name `HDTV`.
   - Detected `2.40:1`: display ratio `2.40:1`; name `Scope`.
3. For any other valid detected ratio, display its measured ratio in normalized
   `width:height` form (for example `1.85:1`) and set the name to `Unknown`.
   Do not apply unrequested names such as Academy, Flat, IMAX, or CinemaScope.
   Keep the ratio visible even when its name is `Unknown`.
4. Define and test a matching tolerance around each of the three named ratios.
   Match against the precise detected value rather than the formatted string.
   Keep 1.33:1 and 1.78:1 distinct and avoid absorbing nearby unnamed formats
   into a named range. Treat 2.39:1 and 2.40:1 as separate nominal inputs when
   validating that the Scope tolerance does not label unintended ratios.
5. If no valid aspect ratio is available, use the existing OSD unavailable
   value for the ratio and show `Unknown` for the name. Do not fabricate a
   measurement.
6. Update only when the displayed ratio or name changes; avoid per-frame OSD or
   log churn. Retain the VP Renderer Ctrl+I OSD's existing right-side placement,
   typography, lifetime, and visibility rules. Do not add the values to madVR or
   DirectShow OSD output, the separate Ctrl+Alt+I profile overlay, or another
   panel or shortcut.
7. Update relevant help/reference text if needed. Do not add configuration
   unless implementation discovery shows a concrete user need for it.

## Acceptance criteria

1. Tests verify `1.33:1` displays `4:3` / `TV`, `1.78:1` displays `16:9` /
   `HDTV`, and `2.40:1` displays `2.40:1` / `Scope`.
2. Other representative ratios, including 1.37:1, 1.66:1, 1.85:1, 1.90:1,
   2.00:1, 2.20:1, 2.35:1, and 2.39:1, retain a numeric ratio and show
   `Unknown` as the name.
3. Tests cover each named ratio's tolerance boundaries, invalid or unavailable
   measurements, numeric formatting, and ratio changes while the OSD is open.
   Matching uses the measured value, not the rounded display string.
4. Existing VP Renderer Ctrl+I content, toggling, rendering path, and layout
   remain intact. Verify both values fit in the right-side panel and other
   renderer OSDs plus the Ctrl+Alt+I profile overlay remain unchanged.
5. Relevant tests and a successful x64 Release build pass. Record any remaining
   live OSD validation before moving the story to Review.

## Related context

This story applies only to the VP Renderer right-side Ctrl+I statistics OSD.
VP-0158 documents a separate Ctrl+Alt+I active-profile overlay; VP-0161
documents the VP Renderer Ctrl+I health rows. This story consumes the existing
detected aspect ratio and does not alter detection, trust thresholds,
active-picture selection, automatic crop behavior, or other renderer OSDs.

## Readiness and next action

Before implementation, locate the authoritative detected-ratio value and the
existing VP Renderer Ctrl+I OSD surface, then verify that both values can be
added without changing detection or other renderers. Record the matching
precision/tolerance decision and implementation readiness before moving this
story to In Progress.
