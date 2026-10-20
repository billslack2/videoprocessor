# VP-0200: Profile-based resolution and refresh-rate configuration

## Status

In Progress (2026-10-20; deployed for operator validation). Implementation is in `codex/vp-0200-display-tab`
in `E:\codex\videoprocessor\vp-0200-display-tab`, based on the current beta
branch tip `72bcc9ad1fc29142fb91928461e567f5725dcd0b` (`v1.4.02-beta`).
The deployed feature commit is `36219ec53f255e8ac3323171bb60bfcd7271f7ce`. The top-level Display page is directly
below General and outside VP Renderer. The existing refresh-switch setting is
shown on Display while retaining its existing `general.switch_refresh_rate`
key and options. The new `output_display` profile family supports Auto and
Advanced, ordered exact-rational modes, add/remove/reorder, DXGI/GDI availability,
and a cadence preview that does not change the display. Profile status is
carried through the live-status RPC. Configuration help and migration are
implemented. VP-0172 supplies the single shared mode parser/ranker and runtime
mode transaction; Auto retains the existing refresh-only path.

Previous validation (2026-10-19): the full x64 Release solution build succeeds,
including the player, Config, and both renderer variants. The official Magewell
SDK was already installed at
`E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3`; isolated worktree builds
require the documented `MagewellSdkRoot` property. The earlier missing-SDK
report was incorrect. Fetching GitHub again confirmed that this worktree's
base `d988e54b883381ad13371f593593773a5e681c51` still equals the current
`v1.4.02-beta` tip and includes the merged subtitle fixes.

All 75 focused backend/config-core tests pass: 31 refresh-policy tests and 44
config-core tests, including seven new regressions for mode parsing, exact
availability, ordered resolution selection, interlaced cadence, profile
persistence, and live apply/reordering. Full-build validation exposed and fixed
a C++ compatibility issue in rational reduction, a runtime whitelist rejecting
`output_display.*`, missing live-apply/profile-order classification, and an
unrelated second-pass migration of newly seeded Motion defaults. Display also
no longer falls through into unrelated HDR metadata controls.

Focused Config checks pass for Apply/OK/Cancel, display-warning precedence,
target refresh, and remote target responsiveness. The broader renderer-profile
UI check verifies Display navigation and the moved refresh selector, then stops
at an inherited scaling accessibility assertion: the test expects `Anti-ringing`
while both the unchanged beta source and current UI use `Sigmoid upscaling`.
That mismatch remains outside this implementation's backend validation.

At that point, no VP-0200 binaries had been deployed and no active configuration had been edited.
Actual display switching, rollback, restore ownership, and scene-dependent
hardware behavior still require bench validation. Keep this story In Progress.
## Rebase and deployment (2026-10-20)

The user authorized rebasing onto the latest beta and deployment. GitHub's
default and current beta were both `v1.4.02-beta`, fetched at
`72bcc9ad1fc29142fb91928461e567f5725dcd0b`. The feature rebased cleanly to
`4dae9701fb935b371df8119160446e554133e68b` and is pushed on
`codex/vp-0200-display-tab`. The incoming subtitle fix `f0a416ed` (PR #170)
remains intact: prepared subtitle erasure cannot change picture placement.

A fresh full x64 Release Rebuild succeeded from the clean feature commit.
Generated host and renderer identities both show that commit with dirty=false.
All 360 selected native regressions passed (zero failed/skipped), covering
Display refresh policy, config core, ActivePictureEvidence, and both subtitle
cut/paste suites, including all six new prepared-erasure regressions. All four
focused Config checks passed: Apply/OK/Cancel, display-warning precedence,
inline target refresh, and remote target responsiveness.

Deployed the matching player, both renderer variants, Config, discovery DLL,
their genuine Release runtime records, and updated configuration help to
`C:\Videoprocessor\vp`. All 11 installed file hashes were verified against
build/source inputs. Private Qt and renderer dependencies already matched.
The installed Config executable passed a native Display-page screenshot smoke
check on a scratch copy of the active settings.

Backup and verified receipt:
`C:\Videoprocessor\vp\backups\deploy-20261020-104733-vp0200-4dae9701`.
The backup includes every replaced file plus `VideoProcessor.cfg` and
`VideoProcessor.state`. Both active user-data files remain byte-identical;
configuration edits: none. The background Config process was restarted. The
player was stopped before deployment and remains stopped.

Local evidence is under
`E:\codex\videoprocessor\vp-0200-display-tab\artifacts`: the rebased Release
build log, `test-results\vp0200-rebased-regressions.trx`, and
`vp0200-deployed-display.png`. Hardware mode switching, rollback, restoration,
and playback of the reported scene remain operator/bench acceptance work.
## Monitor selection and local availability fix (2026-10-20)

The operator confirmed **one monitor per Display profile**. The Monitor control
now lives on Display and persists as `output_display.<profile>.monitor`; `auto`
means the Windows primary monitor. Named variants inherit the baseline monitor
until overridden. The editor preserves a legacy `fullscreen_monitor_name` in
the baseline profile, removes only the migrated legacy keys on Apply/OK, and
uses the existing migration backup. Deployment itself made no configuration edits.

The local failure was real: discovery used QueryDisplayConfig friendly names,
while availability tried to resolve them through EnumDisplayDevices strings.
Availability also stopped after the first GPU adapter and was explicitly disabled
for RPC connections, including loopback. A shared Win32 inventory now resolves
friendly/GDI/disambiguated names consistently for discovery, Config, playback,
and optional target-only topology selection. DXGI and progressive 32-bit GDI
modes are enumerated on the selected output, across adapters.

A bounded read-only GetDisplayModes RPC supplies inventory from the VP machine.
Config refreshes asynchronously on profile/monitor/topology changes or Refresh,
coalesces requests, and rejects results for stale profiles/targets. Missing or
ambiguous monitors and query failures produce explicit explanations. The runtime
snapshot carries the selected monitor, places an existing fullscreen window
without taking focus, and prevents mode changes when the playback window is on
a different output or the configured monitor is unavailable.

Source commits `1d3b6ebf` and `36219ec5` are pushed on the existing feature branch.
The beta integration tip remains `72bcc9ad1fc29142fb91928461e567f5725dcd0b`;
the prepared subtitle-erasure fix is still included. A full x64 Release Rebuild
and the final pinned Release Build succeeded. Host and renderer identities
show `36219ec53f255e8ac3323171bb60bfcd7271f7ce`, dirty=false.
No automated suites were rerun for this follow-up; the earlier 360-test and
four-UI-check results above belong to the previous deployed commit.

The actual installed native Config UI, using scratch settings and read-only
hardware enumeration, reports **72 progressive modes for EPSON PJ** and
**106 for the Windows primary monitor**. Both example Epson entries
1920x1080p60 and 3840x2160p23.976 are available; preview selects the latter
for 23.976 input. The examples were not applied to the active configuration.

- [Installed real UI with Epson availability](../assets/VP-0200/installed-epson-monitor-20261020.png)
- Local evidence: `E:\codex\videoprocessor\vp-0200-display-tab\artifacts`,
  `vp0200-monitor-pinned-release.log`, `vp0200-deployed-epson-monitor.png`.
- Deployment backup/receipt:
  `C:\Videoprocessor\vp\backups\deploy-20261020-112200-vp0200-36219ec5`.
  All 11 replaced software/document/runtime-record hashes match build inputs.
  Matching player, both renderers, Config and discovery DLL were deployed.
  Every replaced file, `VideoProcessor.cfg` and `VideoProcessor.state` was backed
  up. Active cfg/state remain byte-identical; configuration edits: **none**.
  Background Config was restarted; playback was stopped and remains stopped.

Live mode application, monitor transitions during playback, remote VP RPC,
rollback, and restoration still need operator/bench qualification. Keep this
story In Progress.

## User story

As a VP Renderer user, I want display-mode profiles with an obvious Auto default
and an Advanced option, so I can preserve existing behavior or explicitly choose
permitted output resolutions and refresh rates for my display.

## Scope and ownership

This story owns the configuration model, editor, migration, documentation, and
profile integration for the design discussed in chat. VP-0172 owns the shared
complete-mode selector and display transaction. Integration is resolved: the
`output_display` profile feeds one renderer settings path, the editor preview
uses the same VP-0167 rational ranking helper as runtime, Auto retains the
existing refresh-only behavior, and Advanced passes its ordered allowed modes
to the existing display transaction. Do not add a second mode selector.

## Configuration and UI contract

- Add **Config > Display** as a top-level page directly below General in the
  left navigation, outside VP Renderer. Move Monitor into each Display profile,
  with one selector controlling fullscreen playback, availability, and preview.
  Keep the other General settings in General. The refresh-switch control remains
  on Display, with its existing shared general key and one owner.
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

Complete focused parser/profile/UI acceptance checks and bench validation for
mode application, rollback, restoration ownership, DCI selection, and source
changes. Resolve the inherited scaling-label UI test mismatch separately.
