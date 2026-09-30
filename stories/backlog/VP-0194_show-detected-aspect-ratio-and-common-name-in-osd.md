# VP-0194: Show detected aspect ratio and common format name in the VP Renderer Ctrl+I OSD

## Status

Backlog. Add one ratio row to the existing right-side VP Renderer Ctrl+I OSD.
Append a recognizable common name when the value matches a supported format,
while preserving the numeric measurement as the source of truth.

## User story

As a VideoProcessor operator, I want the VP Renderer Ctrl+I OSD on the right
side to show the detected active-picture aspect ratio and a common format name
when one applies, so I can quickly understand what shape of picture VP is
detecting.

## Scope and required behavior

1. Add a single `Aspect ratio` row to the existing right-side Ctrl+I OSD when
   VP Renderer is active. Show this row only on that VP Renderer OSD. Use the
   authoritative current detection result and do not add a new detector, crop
   policy, or aspect-ratio override as part of this story.
2. Always show the numeric ratio in a stable, readable `width:height` form,
   normalized to height 1 (for example `1.78:1`). Keep enough precision to
   distinguish nearby formats. A common name supplements the measurement and
   never replaces it.
3. Append a common name only when the detected value matches a supported named
   format under an explicit, tested matching tolerance. Show the numeric ratio
   alone when it does not match. Do not present the OSD's rounded display text
   as the value used for matching.
4. Use this starter vocabulary for common names:
   - `1.33:1` — `4:3` (standard-definition video / traditional television).
   - `1.37:1` — `Academy`.
   - `1.43:1` — `IMAX 70mm`.
   - `1.66:1` — `European widescreen`.
   - `1.78:1` — `16:9` (HD video / widescreen television).
   - `1.85:1` — `Flat` (theatrical widescreen).
   - `1.90:1` — `Digital IMAX`.
   - `2.00:1` — `Univisium`.
   - `2.20:1` — `70mm` / `Todd-AO`.
   - `2.33:1` — `21:9` (consumer ultrawide, approximate label).
   - `2.35:1` — `CinemaScope` (historical scope ratio).
   - `2.39:1` — `Scope` (modern theatrical anamorphic ratio).
   - `2.40:1` — `Scope` (common rounded presentation of 2.39:1).
   - `2.76:1` — `Ultra Panavision 70`.
5. The matching policy must handle nominal-equivalent forms without conflating
   meaningfully different formats. In particular, 2.35:1, 2.39:1, and 2.40:1
   must not all be labeled `CinemaScope` solely because they are close. Define
   and test the tolerance against realistic detector variation and boundary
   cases. Keep names easy to extend in one place.
6. Update only when the displayed ratio/name changes; avoid per-frame OSD or
   log churn. Retain the VP Renderer Ctrl+I OSD's existing right-side placement,
   typography, lifetime, and visibility rules. Do not add the row to madVR or
   DirectShow OSD output, the separate Ctrl+Alt+I profile overlay, or another
   panel or shortcut. Verify portrait and unusual ratios remain legible and
   fall back to the numeric ratio without a misleading name.
7. Update relevant help/reference text if the OSD's detected ratio or names
   need explanation. Do not add configuration unless implementation discovery
   shows a concrete user need for configurable naming.

## Acceptance criteria

1. For known test geometry, the VP Renderer Ctrl+I OSD displays the correct
   active-picture ratio normalized to height 1, with enough precision for
   supported names.
2. A named common format displays its numeric value and the expected name.
   Unsupported and portrait ratios display the numeric value without an
   incorrect name.
3. Tests cover each starter name, matching tolerance boundaries, nearby
   distinct ratios (especially 1.85/1.90 and 2.35/2.39/2.40), invalid or
   unavailable measurements, and ratio changes while the OSD is open.
4. The name matching uses the precise detected value, not a rounded string.
   OSD updates are change-triggered and do not add per-frame diagnostic churn.
5. Existing VP Renderer Ctrl+I content, toggling, rendering path, and layout
   remain intact; verify readable placement in its right-side panel, including
   long names such as `Ultra Panavision 70`. Other renderer OSDs and the
   Ctrl+Alt+I profile overlay remain unchanged.
6. Relevant tests and a successful x64 Release build pass. Record any remaining
   live OSD validation before moving the story to Review.

## Related context

This story applies only to the VP Renderer right-side Ctrl+I statistics OSD.
VP-0158 documents a separate Ctrl+Alt+I active-profile overlay; VP-0161
documents the VP Renderer Ctrl+I health rows. This story consumes the existing
detected aspect ratio and does not alter detection, trust thresholds,
active-picture selection, automatic crop behavior, or other renderer OSDs.

The starter labels reflect commonly cited theatrical and video formats. Some
labels are approximate or refer to presentation traditions rather than a
single exact raster ratio; keep the numeric measurement visible and document
the chosen matching tolerance in implementation.

## Readiness and next action

Before implementation, locate the authoritative detected-ratio value and the
existing VP Renderer Ctrl+I OSD surface, then verify whether the value is
accessible without changing renderer interfaces. Confirm the intended display
precision, matching tolerance, and final starter labels against that data path.
If those choices can be made without a new detector or pipeline dependency,
record the readiness review and implement the OSD addition.
