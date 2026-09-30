# VP-0194: Show aspect ratio and name in the VP Renderer Ctrl+I OSD

## Status

Backlog. Add `Aspect ratio` and `Name` values to the existing right-side VP
Renderer Ctrl+I OSD. Snap measurements within 1% of a supported common ratio;
show `Unknown` for values that do not match a supported ratio.

## User story

As a VideoProcessor operator, I want the VP Renderer Ctrl+I OSD on the right
side to show the detected active-picture ratio and its name, so I can quickly
understand what shape of picture VP is detecting.

## Scope and required behavior

1. Add `Aspect ratio` and `Name` values to the existing right-side Ctrl+I OSD
   when VP Renderer is active. Show them only on that VP Renderer OSD. Use the
   authoritative current detection result; do not add a new detector, crop
   policy, or aspect-ratio override.
2. Use these known ratio mappings:
   - `1.33:1` snaps to displayed ratio `4:3`; name `TV`.
   - `1.66:1` snaps to displayed ratio `1.66:1`; name `European widescreen`.
   - `1.78:1` snaps to displayed ratio `16:9`; name `HDTV`.
   - `1.85:1` snaps to displayed ratio `1.85:1`; name `Flat`.
   - `1.90:1` snaps to displayed ratio `1.90:1`; name `Digital IMAX`.
   - `2.39:1` snaps to displayed ratio `2.39:1`; name `Scope`.
   - `2.40:1` snaps to displayed ratio `2.40:1`; name `Scope`.
3. For each supported ratio, snap the detected value when the relative
   difference `|detected - target| / target` is 1% or less, inclusive. Targets
   are the exact values `4/3`, `1.66`, `16/9`, `1.85`, `1.90`, `2.39`, and
   `2.40`. If the value is within 1% of multiple targets, choose the closest by
   relative difference; use the lower target as a deterministic tie-break.
   Display the chosen canonical ratio/alias and name. The 2.39:1 and 2.40:1
   targets are both supported and must resolve independently to the closest
   target.
4. If no supported target is within 1%, display the measured ratio in normalized
   `width:height` form (for example `1.37:1`) and set the name to `Unknown`.
   Do not apply an unlisted label. Keep the measured ratio visible with
   sufficient precision when no snap applies.
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

1. Tests verify all seven named mappings and the exact ratio/name text shown in
   the OSD.
2. For each supported target, tests cover measurements below, exactly at, and
   above the 1% relative-difference boundary. Inputs within range snap to the
   closest target; inputs outside all ranges retain their measured ratio and
   show `Unknown`.
3. Tests cover values between 2.39:1 and 2.40:1, including overlapping match
   ranges, nearest-target selection, and the deterministic tie-break.
4. Other representative ratios, including 1.37:1, 1.43:1, 2.00:1, 2.20:1,
   2.33:1, 2.35:1, and 2.76:1, retain a numeric ratio and show `Unknown` as the
   name.
5. Tests cover invalid or unavailable measurements, numeric formatting, and
   ratio changes while the OSD is open. Matching uses the measured value, not
   the rounded display string.
6. Existing VP Renderer Ctrl+I content, toggling, rendering path, and layout
   remain intact. Verify both values fit in the right-side panel and other
   renderer OSDs plus the Ctrl+Alt+I profile overlay remain unchanged.
7. Relevant tests and a successful x64 Release build pass. Record any remaining
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
