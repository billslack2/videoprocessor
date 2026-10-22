# VP-0200: Profile-based resolution and refresh-rate configuration

## Status

In Progress (2026-10-21; reviewed software merged into beta;
projector-on acceptance pending). [PR #184](https://github.com/billslack2/videoprocessor/pull/184)
is merged and pushed to `v1.4.02-beta` at
`acb0cf6f4c5e9bce045cade13e7822b6f4f2a810`. Its parents are the verified beta
`71043e192b658c433dc8d888632f4740d1e61b01` and tested combined head
`921791d0768a18df4087cbc09a97110bd609aefc`. The fetched remote merge tree is
byte-for-byte the tested source tree (`770923e6e1330f32388711e9949ec474ed81490c`).

The merge preserves #181/#183 history and review fixes for stale raster
snapshots, recreated swapchain dimensions and clipped OSD annotations. All 26
added tests passed. The #181 layer `3a8f8a08` passed all 2,948 native tests;
the final combined head passed 378 targeted regressions and four Config checks.
Both clean x64 Release solution builds passed. The final combined head did not
run the entire 2,948-test suite again; no tests were rerun for this unchanged-tree
merge. GitHub also marks #181 merged. Original fork #183 remains open at
`1669ee64`; its original commits and upstream review fixes are included in #184,
so it does not need a second merge. No deployment or active configuration/state
edit was performed for this merge. Source/build/test pins and merge verification
are in the [review evidence](../assets/VP-0200/pr181-pr183-review-20261021.md).

Previous operator deployment (2026-10-20): implementation is in `codex/vp-0200-display-tab`
in `E:\codex\videoprocessor\vp-0200-display-tab`, based on the then-current beta
branch tip `72bcc9ad1fc29142fb91928461e567f5725dcd0b` (`v1.4.02-beta`).
The deployed feature commit is `5101c22a638316628c0cea878744bcb85e7b9b25`. The top-level Display page is directly
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

## Fixed output and recommended mode picker (2026-10-20)

Source `5101c22a638316628c0cea878744bcb85e7b9b25` is committed and pushed on
`codex/vp-0200-display-tab`, still based on beta `72bcc9ad1fc29142fb91928461e567f5725dcd0b`.
The full clean x64 Release Rebuild succeeded with zero errors; host/renderer
identities show this commit with dirty=false. Automated suites were not run
for this follow-up; one existing interlaced expectation was updated for the
new explicit allowed-mode fallback.

The operator's single available 2560x1440p60 entry was previously rejected by
the preview's independent cadence filter for 24 fps. Advanced now ranks cadence
matches first, followed by remaining available allowed entries in configured
order. One available entry therefore means fixed output for any valid source
cadence. Preview and renderer share this selector and explicitly identify a
configured fallback. Auto retains its existing refresh-only ranking. Missing
allowed timings still leave the current display mode unchanged.

The Add mode catalog is grouped by descending raster size and descending
refresh. Whole-number labels (p60, p59, p24, p23) retain exact rational timing
in item data and saved configuration; accepting an existing choice never
reparses its shortened label. Near-identical driver timings share a label,
with integer/broadcast timings preferred. Saved list order remains playback
fallback priority.

Windows DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE supplies the
preferred resolution for the selected physical output, including through
inventory RPC version 2 (new clients also read v1 inventories). This is a
recommendation rather than proof of panel-native pixels. The picker defaults
to that resolution plus reported HD/UHD video resolutions related by integer
scaling, with Show all supported resolutions available. The filter only
changes the catalog view; saved modes and runtime inventory are preserved.
If the API does not report a preferred resolution, all resolutions are shown.
[Microsoft API reference](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_target_preferred_mode).

Actual native UI captures on scratch settings show:

- LG preferred resolution 2560x1440: **4** recommended whole-number choices,
  in order 75, 74, 60, 59 Hz labels.
- Epson preferred resolution 3840x2160: **12** recommended choices, grouped
  3840x2160 then 1920x1080; each group is ordered 60, 59, 50, 25, 24, 23.
- The exact screenshot case, source cadence **24** with only 2560x1440p60,
  now previews that entry as fixed output.
- [Real fixed-output preview](../assets/VP-0200/fixed-output-preview-20261020.png)
- [Real LG picker](../assets/VP-0200/lg-mode-picker-20261020.png)
- [Real Epson picker](../assets/VP-0200/epson-mode-picker-20261020.png)

Deployment was initially held to preserve unsaved editor changes. The operator
then explicitly requested deployment, and neither Config nor playback was running
at the deployment check. Deployed `5101c22a638316628c0cea878744bcb85e7b9b25`
on 2026-10-20 at 15:07 America/New_York. The GitHub default/current beta remained
`v1.4.02-beta` at `72bcc9ad1fc29142fb91928461e567f5725dcd0b`.

Backup and verified receipt:
`C:\Videoprocessor\vp\backups\deploy-20261020-150743-vp0200-5101c22a`.
All 11 deployed player/Config/discovery/renderer/help/runtime-record file hashes
match the clean x64 Release build inputs. Both renderer variants and the player
are from the same commit. Each replaced file plus active `VideoProcessor.cfg`
and `VideoProcessor.state` was backed up. Both user-data files remain
byte-identical; configuration edits: **none**. Neither application was restarted;
playback remains stopped. Live mode application and remote RPC remain
operator/bench qualification work.
Local evidence: `artifacts/vp0200-picker-release.log`,
`vp0200-picker-preview.png`, and `vp0200-picker-*-catalog.png[.choices.png]`.

## Tester signal-raster follow-up (2026-10-21)

The user supplied `vp-log-2026-10-21.zip.txt` (a ZIP with only `vp.log`) and
then `VideoProcessor.cfg.txt`. The config is valid: one Advanced `Under_30`
profile, monitor `auto`, restoration enabled, fullscreen-only switching, and
1080p50/59.94/60 followed by 2160p30/29.97/25/24/23.976. It matches the log.
At 00:21:09 and 00:23:01, CCD requests for 1080p59.94/60/50 left the target
signal at 3840x2160 with the requested refresh. Verification rejected them and
rollback succeeded; configured-order fallback then selected 2160p30 before
2160p29.97. 23.976 mode switches and restoration verified in that same log.
The tester's separate GDI probe success and 200% DPI symptom are reported
observations, not independently established by the supplied log.

The fix uses one progressive GDI fullscreen request for raster changes where
an equivalent enumerated selector exists (59 for 59.94), retaining CCD for
rate-only transitions and custom DXGI timings without a GDI selector. Read-back
requires actual source and target modes, signal vSync frequency and progressive
scan; a GPU-scaled desktop is never accepted as the requested physical raster.
Unverified transitions roll back and verify before another candidate; failed
rollback stops attempts. CCD restoration captures original target timing and
scaling. A failure at one rate does not blacklist other rates at that raster.

Shared preview/runtime ranking now puts exact integer cadence multiples and
divisors after native/close candidates and before arbitrary configured-order
fallbacks. Thus the supplied 59.94 profile tries 2160p29.97 after the two native
59.94/60 candidates, ahead of 1080p50 and 2160p30. The config needs no edit.

VP Renderer fullscreen creation and refitting use physical per-monitor DPI
coordinates; client size is re-read after mode selection, and later display
notifications schedule swapchain resizing on the renderer thread. Legacy
DirectShow hosts keep their prior context. Host recreation across backend
changes occurs after retirement; ordinary source/profile restarts retain it.
New diagnostics show API route, desktop/signal dimensions and physical
client/swapchain geometry. Source documentation:
`docs/VP-0200_DISPLAY_MODE_SWITCHING.md`.

Release evidence: full x64 Release Rebuild succeeded from clean commit
`4fd790cec52077f1d559e11da812b17a841ed9be` (72 warnings, zero errors).
Host and renderer identities show `v1.4.02-beta-4fd790c`, dirty=false. The
player, both renderer variants and Config hashes match their genuine x64
Release runtime records. Source branch is pushed. Build log and receipt are
under the fix worktree's `artifacts/vp0200-wire-raster-beta-release.log` and
`artifacts/vp0200-wire-raster-build-receipt.json`. No new automated tests were added or
run. Repeated 4K/1080p transitions on the reported NVIDIA 511.17 driver at 200%
scaling, physical receiver timing, native OSD sizing, stop restoration,
rejected-mode rollback, external changes and custom timings remain bench
qualification. No active configuration, state or deployed binaries were changed.

## Successful tester run (2026-10-21)

Analysis of the newly supplied `vp-log-pr181-2026-10-21.zip.txt` confirms six
first-candidate GDI 1080p59.94 selections with 1920x1080 client and swapchain on
every selection; five successful CCD 2160p23.976 requests; six verified GDI
restores to 4K23.976; and no mode unverified/rejected/rollback/failure messages.
1080p candidate-to-verification intervals are 0–1 seconds at one-second log
precision. Eligible lists put 29.97 before arbitrary 50/30 Hz fallback, but no
fallback or failure recovery was exercised because first candidates succeeded.

The tester reports the same RTX 3060 Ti / 511.17 rig at 200% scaling, with
#181 `4fd790ce` on `88289699` plus their open PRs. Both modules actually log
clean combined build `07ab5fffc593fdb77e2d07a38786d33e193f4a60`.
The projector was off and TightVNC was used: mode read-back and geometry are
qualified for this run; projected picture, independent receiver timing and
visually checked native OSD/1:1 mapping are not. This does not qualify the new
beta `71043e19` combination. No new local tests, source changes, merge,
deployment or config edits were performed.

[Detailed evidence and remaining acceptance](../assets/VP-0200/pr181-tester-validation-20261021.md).
The #181 description now records this partial qualification. The announced
[OSD raster PR #183](https://github.com/billslack2/videoprocessor/pull/183)
is open and stacked on #181 at `4fd790ce`; review/integration is separate.

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
- With no available permitted mode, retain the current mode and continue playback
  with a clear diagnostic. Advanced profiles fall back to available allowed modes
  after native/close matches and exact integer cadence multiples/divisors;
  remaining fallbacks follow configured order and one available entry is fixed output. Apply and verify full modes through VP-0172; restore
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
   ordering, fixed-output fallback, compact-label round trips, preferred-resolution
   filtering and Show all are covered by focused tests and UI checks.
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
