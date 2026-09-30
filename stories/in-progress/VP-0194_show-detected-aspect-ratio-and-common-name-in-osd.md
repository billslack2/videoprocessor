# VP-0194: Show aspect ratio and name in the VP Renderer Ctrl+I OSD

## Status

In Progress (2026-09-30). Implemented on source branch
`codex/vp-0194-aspect-osd` at `c1824064`, based on verified GitHub
`v1.3.005-beta` tip `7b50c4339dcd6c6ad3f74ac53bf4cb2c3d661ff3`.
Draft review: [PR #124](https://github.com/billslack2/videoprocessor/pull/124).
Live Ctrl+I visual validation remains pending before review completion.

## User story

As a VideoProcessor operator, I want the VP Renderer Ctrl+I OSD on the right
side to show the detected active-picture ratio and its name, so I can quickly
understand what shape of picture VP is detecting.

## Scope and required behavior

1. Add `Aspect ratio` and `Name` values to the existing right-side Ctrl+I OSD
   when VP Renderer is active. Show them only on that VP Renderer OSD. Use the
   authoritative current detection result; do not add a new detector, crop
   policy, or aspect-ratio override.
2. Use the complete supported ratio/name list below. Display ratio aliases for
   TV and HDTV; for other named formats display the canonical numeric ratio.
   - `1.33:1` → ratio `4:3`; name `TV`.
   - `1.37:1` → ratio `1.37:1`; name `Academy`.
   - `1.43:1` → ratio `1.43:1`; name `IMAX 70mm`.
   - `1.66:1` → ratio `1.66:1`; name `European widescreen`.
   - `1.78:1` → ratio `16:9`; name `HDTV`.
   - `1.85:1` → ratio `1.85:1`; name `Flat`.
   - `1.90:1` → ratio `1.90:1`; name `Digital IMAX`.
   - `2.00:1` → ratio `2.00:1`; name `Univisium`.
   - `2.20:1` → ratio `2.20:1`; name `70mm / Todd-AO`.
   - `2.33:1` → ratio `21:9`; name `Consumer ultrawide` (approximate).
   - `2.35:1` → ratio `2.35:1`; name `CinemaScope` (historical).
   - `2.39:1` → ratio `2.39:1`; name `Scope`.
   - `2.40:1` → ratio `2.40:1`; name `Scope`.
   - `2.76:1` → ratio `2.76:1`; name `Ultra Panavision 70`.
3. On the first trusted ratio for a source, choose the closest supported target
   within 1% relative difference (`|detected - target| / target`), inclusive.
   Targets are the exact values `4/3`, `1.37`, `1.43`, `1.66`, `16/9`, `1.85`,
   `1.90`, `2.00`, `2.20`, `21/9`, `2.35`, `2.39`, `2.40`, and `2.76`. If more
   than one target qualifies, choose the closest; break an exact tie toward the
   lower target. This selects 2.39:1 or 2.40:1 from a precise initial reading.
4. Once a target is selected, retain its displayed ratio and name through
   ambiguous readings, including variation up to 3% relative to that held
   target. Do not switch merely because a nearby target becomes momentarily
   closer. Switch only after the existing detector confirms a genuine, stable
   new active-picture aspect. If it offers no such confirmation signal, require
   sustained readings that clearly favor the new target over the held target;
   define the margin and confirmation duration at implementation readiness.
   A single outlier must not switch the label. On a new source or detector
   epoch, acquire from its current trusted ratio rather than a stale label.
   A confirmed new aspect is reclassified using the 1% nearest-target rule.
   If it has no target within 1%, display the measured ratio and `Unknown`.
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

1. Tests verify all 14 supported mappings and the exact ratio/name text shown in
   the OSD.
2. On initial acquisition and confirmed aspect changes, tests cover values
   below, exactly at, and above each 1% relative-difference boundary. Values
   within range snap to the closest target; values outside all ranges retain
   their measured ratio and show `Unknown`.
3. Tests cover overlapping match ranges among 2.33:1, 2.35:1, 2.39:1, and
   2.40:1, including nearest-target selection and the exact tie-break. Exact
   initial 2.39:1 and 2.40:1 readings select their respective targets.
   Ambiguous jitter, including deviations up to 3% from the held target,
   preserves the held label. A single outlier does not switch it; a sustained,
   confirmed new aspect does. A new source reacquires its own initial label.
4. Representative unsupported ratios such as 1.50:1 and 2.50:1 retain their
   measured ratio and show `Unknown` as the name.
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

## Implementation and validation (2026-09-30)

- The OSD reads VP Renderer's committed, stable active-picture rectangle
  through a nonblocking renderer snapshot. It uses the detector's source and
  publication generations. Detector and crop policy are unchanged.
- An OSD-only label state applies the 1% nearest-target rule to the unrounded
  measured ratio. Exact initial 2.39 and 2.40 readings choose their respective
  targets. A competing label within 3% of the held target needs 10 consecutive
  one-second OSD reads of the same committed publication; a clearer change
  needs 3. A source-generation change resets the label immediately.
  Unavailable detection shows `---` and `Unknown`; a busy renderer read
  retains the prior display.
- The two rows are gated by the VP Renderer flag in the existing Ctrl+I
  bitmap. Height includes both rows. DirectShow and the profile overlay are
  untouched.
- Full solution x64 Release build passed. Two focused Release tests passed,
  covering all 14 mappings, isolated 1% boundaries, overlap and tie cases,
  unknown formatting, 3% hold, and source reacquisition.
- Still needed: view the right-side native Ctrl+I OSD with a live source,
  confirm both rows fit and update on a real aspect transition, and confirm
  the other renderer/profile overlays visually. No deployment has occurred.
