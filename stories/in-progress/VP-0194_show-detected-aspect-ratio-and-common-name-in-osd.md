# VP-0194: Show aspect ratio and name in the VP Renderer Ctrl+I OSD

## Status

In Progress (2026-09-30). Implemented on source branch
`codex/vp-0194-aspect-osd` at `3280e496`, based on verified GitHub
`v1.3.005-beta` tip `7b50c4339dcd6c6ad3f74ac53bf4cb2c3d661ff3`.
Draft review: [PR #124](https://github.com/billslack2/videoprocessor/pull/124).
Live Ctrl+I visual validation remains pending before review completion.

## User story

As a VideoProcessor operator, I want the VP Renderer Ctrl+I OSD on the right
side to show the detected active-picture ratio and its name, so I can quickly
understand what shape of picture VP is detecting.

## Scope and required behavior

1. Show one `Aspect Ratio: <ratio> (<name>)` row in the existing right-side
   Ctrl+I OSD when VP Renderer is active. Show them only on that VP Renderer OSD. Use the
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
   remain intact. Verify the combined row fits in the right-side panel and other
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
- The one-line aspect row is gated by the VP Renderer flag in the existing
  Ctrl+I bitmap. Height includes that row. DirectShow and the profile overlay
  are untouched.
- Full solution x64 Release build passed. Two focused Release tests passed,
  covering all 14 mappings, isolated 1% boundaries, overlap and tie cases,
  unknown formatting, 3% hold, and source reacquisition.
- Still needed: view the right-side native Ctrl+I OSD with a live source,
  confirm the combined row fits and updates on a real aspect transition, and confirm
  the other renderer/profile overlays visually. Deployment evidence is recorded below.

## Deployment (2026-09-30)

- Deployed the host and VP Renderer DLL together from the successfully
  completed x64 Release build of clean source commit
  `c1824064ac97e0763cc94dfb112fff4e32f94424`. Both generated version
  headers reported that commit with `VERSION_DIRTY=false`.
- The previous installed pair was backed up at
  `C:/Videoprocessor/vp/backups/vp-0194-20260930-095854-201`.
  The deployment receipt is `deployment.json` in that folder.
- Independent SHA-256 verification after copying matched the build artifacts:
  host `D25CFA0B7796F44C1281CC0D1FF1A2DFE8C8C814F58AE52E45ACD886F4DFE59E`;
  VP Renderer DLL `1E4AB195A94A418E1166A7D67D27AC574F008A2A058804BB6062A7DCF9824225`.
- No configuration or state files were edited. VideoProcessor was stopped
  before and after deployment; live Ctrl+I visual validation remains pending.

## User feedback and correction (2026-09-30)

- The first deployed build showed `Aspect ratio: ---` and `Name: Unknown`
  with `NLS: Off`. Fullscreen briefly showed HDTV, then a display transition
  returned the OSD to Unknown. Investigation found that VP Renderer skipped
  active-picture analysis when NLS, automatic crop, and other consumers were
  off. The screenshot is evidence that the row rendered but had no trusted
  detector result.
- Source commits `e67a13d3` and `a83e3dca` make the visible Ctrl+I stats
  panel request the existing detector without enabling NLS or automatic crop,
  retain a trusted OSD label for up to nine one-second reads during temporary
  detector withdrawal, and show ratio and name together as
  `Aspect Ratio: 2.39:1 (Scope)`. A genuine new source still reacquires.
- A serial full x64 Release rebuild and focused aspect OSD tests passed after
  the detector correction. The final clean-commit x64 Release build passed
  with both version headers reporting `a83e3dc` and `VERSION_DIRTY=false`.
- Replaced the installed host and renderer DLL together from
  `a83e3dca3c4c0e3d362deed1e34fcf3b6cc610f5` after confirming VP was
  closed. The previous pair is backed up at
  `C:/Videoprocessor/vp/backups/vp-0194-redisplay-20260930-104425-005`;
  see its `deployment.json` receipt.
- Independent deployed SHA-256 matches the new build artifacts: host
  `C44E56CE37BFEF761AF89C370E4FD4A4977192A97FB906EA316B94E214C3504D`;
  renderer `D9906F65D9B9EBD4A01888F47F0C44DA946C78F7010DD01A1EBD1E5E82181923`.
  Configuration and state files were not edited. Live Ctrl+I verification of
  NLS-off detection and fullscreen/windowed continuity remains pending.

## OSD responsiveness correction (2026-09-30)

- User reported that HDTV-to-IMAX changes in *Eternals* reached the OSD too slowly. The previous implementation checked the ratio only once per second and then required three OSD reads for clear changes.
- Source commit `3280e496` polls the VP Renderer aspect snapshot every 150 ms only while the Ctrl+I OSD is visible. It rebuilds and submits the bitmap only when the displayed ratio or name changes; the rest of the statistics keep their existing one-second cadence.
- A clear change outside the held target's 3% ambiguity range is displayed as soon as the renderer publishes its already-confirmed stable aspect. A competing label within 3% must remain the same committed publication for five seconds. Temporary detector unavailability keeps the prior trusted label for up to ten seconds, independent of polling frequency. A genuine source-generation change still resets it immediately.
- The full solution x64 Release build passed from clean commit `3280e496`; both generated version headers report that commit and `VERSION_DIRTY=false`. The updated focused test compiles in that build. Live playback timing and the right-side OSD still require user validation.
- Ratio names remain `2.35:1 (CinemaScope)` and `2.39:1` / `2.40:1 (Scope)`. Panavision names equipment, not an aspect ratio, so the detector cannot identify it from picture shape.
## Responsiveness deployment (2026-09-30)

- VideoProcessor was closed before deployment. Installed the host EXE and VP Renderer DLL together from the successful clean x64 Release build of `3280e4960c8ac3f8368afae211918ec794d88aaa`. Both generated version headers reported `VERSION_DIRTY=false`.
- Backed up the previous installed pair to `C:/Videoprocessor/vp/backups/vp-0194-fast-aspect-20260930-111219-133`; its `deployment.json` records both before and after hashes.
- Independent installed SHA-256 verification matched the Release artifacts: host `BE391DFB9C4FE18D30422ED34CA0EC6D2AD414942759049478C8E0CB7F8041ED`; renderer `C614B49591C2B54268B7166307F30EBE672CB72D031CC6D53EAD0E592BC1D400`.
- Configuration and state files were untouched. Live playback timing of the HDTV / Digital IMAX switch still needs user observation.