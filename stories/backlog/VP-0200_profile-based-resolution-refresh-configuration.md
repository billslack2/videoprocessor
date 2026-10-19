# VP-0200: Profile-based resolution and refresh-rate configuration

## Status

Backlog (2026-10-18). User requested that the resolution/refresh configuration
and mockup discussion become a separate new story. Design captured; no source
implementation, build, or deployment is part of this tracker change.

## User story

As a VP Renderer user, I want display-mode profiles with an obvious Auto default
and an Advanced option, so I can preserve existing behavior or explicitly choose
permitted output resolutions and refresh rates for my display.

## Scope and ownership

This story owns the configuration model, editor, migration, documentation, and
profile integration for the design discussed in chat. VP-0172 owns the underlying
complete display-mode selection, application, verification, and restoration.
Implement one shared policy/controller; this story must not introduce a second
mode selector. Resolve the integration boundary with VP-0172 before coding.

## Configuration and UI contract

- Add **Config > VP Renderer > Display modes**, after Color / Output and before
  Scaling. Keep a Configure display modes link beside the playback-monitor
  selector in General. Move the existing refresh-switch control into this page
  without leaving two independently editable owners of the same setting.
- Reuse the existing profile framework: profile list, add/remove/reorder, name,
  shortcuts, cycle shortcuts, and supported capture-input selection rules.
  The first ordered profile is the default; exactly one profile is effective.
- Each profile has **Auto / Advanced radio buttons**. Auto is the default.
- Auto preserves the desktop resolution and existing refresh-rate matching,
  including Never / Fullscreen only / Always behavior. Auto does not imply
  permission to enable switching when the user's existing setting is Never.
- Advanced exposes an ordered list of permitted full resolution/refresh modes,
  with add, remove, reorder, availability indication, and selection preview.
  Keep exact rational rates; distinguish 23.976 from 24 and 59.94 from 60.
- An empty Advanced list is a validation error. Switching to Auto retains the
  Advanced list but makes it inactive. Switching back restores those entries.
- Migration creates a Default Auto profile and preserves existing user values,
  comments, refresh-switch settings, and restoration behavior. Resolve existing
  explicit mode-list configurations deliberately; do not silently discard them.
- Preview explains the selected output mode or why no permitted mode is eligible.
  Preview never changes the real display. Enumerated support is not a claim that
  a mode has been successfully applied to the physical link.
- Use the existing rational eligibility/ranking contract from VP-0167/VP-0172.
  Explicit list preference owns output raster policy. Do not infer output raster
  from subtitle position, detected picture aspect, or source raster.
- Source frame/field cadence and scan type inform rate eligibility. AR, subtitle,
  NLS, scene, or smooth-motion presentation changes must not trigger a display
  mode switch. Scope/IMAX changes inside an unchanged capture format are not
  input-format changes. Capture pixel format is not a subtitle/AR policy.
- Preserve suitable existing custom timings and avoid repeated switching for
  small device clock drift. Coalesce requests and reject stale generations.
- With no eligible permitted mode, retain the current mode and continue playback
  with a clear diagnostic. Apply and verify full modes through VP-0172; restore
  prior full mode only while VP still owns the display change.

## Mockups

The guided profile layout is the design reference. These are illustrative,
interactive prototypes, not shipping controls or implementation evidence.

- [Interactive preview](../assets/VP-0200/display-mode-profiles-preview.html)
- [Auto screenshot](../assets/VP-0200/display-mode-profiles-auto.png)
- [Advanced screenshot](../assets/VP-0200/display-mode-profiles-advanced.png)

## Acceptance criteria

1. Default Auto migration retains current resolution and all existing refresh
   switching choices; no unsolicited output switch occurs on upgrade.
2. Profiles independently retain Auto/Advanced, mode order, and settings across
   save/reload, profile switching, and editor restart; only one is effective.
3. Auto/Advanced round trips preserve inactive Advanced values. Empty Advanced
   lists and malformed rates are rejected with actionable messages.
4. Exact and fractional rates, UHD versus DCI, unavailable modes, duplicates,
   ordering, and no-match fallback are covered by focused tests and UI checks.
5. Preview and runtime use the same selector. Tests prove AR/subtitle/NLS changes
   and smooth-motion output cadence do not cause display switches.
6. Existing refresh-only behavior and fullscreen gating remain intact. Profile
   changes produce at most one necessary mode transition and stale requests do
   not apply after another profile or display becomes active.
7. Integration checks cover successful mode application, rejected modes,
   rollback, external display changes, restoration ownership, and preservation
   of AR/subtitle state during output-only resource rebuilds.
8. Native Config navigation and profile controls remain usable at normal DPI,
   high DPI, and narrow window sizes. Documentation explains HDMI resync delays.

## Dependencies and non-goals

- VP-0172: complete output display modes and verified application/restoration.
- VP-0167: existing refresh-rate candidate policy; confirm current implementation
  and acceptance evidence against the latest beta before implementation.
- Existing profile schema and live-apply behavior require a readiness review.
- Custom timing creation/tuning, EDID edits, capture-format selection, subtitle
  detection, AR algorithms, timestamp/PLL changes, and madVR policy are outside
  this story. No renderer deployment is authorized by creating this record.

## Next step

Review current configuration/profile APIs and the VP-0172 ownership boundary,
then implement and validate this story independently of unrelated playback fixes.
