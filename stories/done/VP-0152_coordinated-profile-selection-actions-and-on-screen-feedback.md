# VP-0152: Coordinated profile selection, actions, and on-screen feedback

## Status

Done — accepted, deployed, and merged on 2026-08-27.

The implementation was completed on branch
`codex/screen-config-color-target` from the confirmed
`origin/v1.3.001-beta` integration tip `2cfbaf2`. Source commit `6242736`
(`feat: coordinate profile selection actions and display`) was merged by
[PR #69](https://github.com/billslack2/videoprocessor/pull/69) into
`v1.3.001-beta` as merge commit `56a9c73`.

This record was created directly in `done` after the developer confirmed the
deployed workflow works well and requested retrospective tracking.

## User story

As a VideoProcessor operator, I want rendering, color calibration, screen
geometry, and output policy to be independently selectable while related
profile actions converge on one coherent state and briefly show the resulting
friendly profile names over the visible picture, so projector-mode changes are
predictable, observable, and usable by external automation.

## Scope

- Separate VP Renderer configuration into independent **Rendering**,
  **Color Config**, **Output**, **Screen Config**, and **Input Processing**
  profile families.
- Let Rec.709 and BT.2020 Color Config profiles own their calibrated color and
  source-transfer behavior without selecting a different Rendering profile.
  Calibrated white/black luminance remains in Rendering under Tone mapping.
- Publish friendly `${color_config}` and `${screen_config}` action variables
  using the visible profile names, while retaining stable internal profile
  identifiers for the existing profile-variable contract.
- Publish color-only and screen-only profile changes through the normal
  committed profile event path so commands such as `set_hdr.bat` can respond
  without requiring an unrelated source-state change.
- Add a per-action **Group** and configurable delay. Newest-state-wins
  coalescing applies only within the same group; zero-delay independent actions
  remain immediate.
- Log action scheduling, batch deduplication, pending-action supersession,
  claim, cancellation, skip, and launch decisions clearly enough to diagnose
  settling and race behavior.
- Add a compact native profile-change overlay anchored inside the visible
  picture. Coalesced profile changes appear side by side with friendly labels,
  blue styling, resolution-based scaling, and consistent external padding.
- Add **General > Display > Profile display**, defaulting to five seconds.
  Values from 1 through 60 specify the complete visible lifetime including the
  fade; 0 is displayed as **Off** and suppresses the overlay. The setting
  applies live without restarting capture or the renderer.
- Keep profile/action publication coherent across renderer readiness and
  committed refresh-rate transitions.

## Acceptance criteria

1. Rendering, Color Config, Output, Screen Config, and Input Processing are
   separate configuration concerns and can be selected independently.
2. Selecting only Rec.709 or BT.2020 Color Config publishes the corresponding
   committed profile change and can trigger a matching external action.
3. `${color_config}` and `${screen_config}` accept and expose the same friendly
   names shown by Config, without a duplicate or divergent variable whitelist.
4. Actions sharing one Group use the newest pending state; actions in different
   groups do not cancel one another.
5. Each action may choose a delay from 0 through 30 seconds, and the log records
   debounce, deduplication, coalescing, claim, cancellation, and launch results.
6. Multiple near-simultaneous profile changes produce one readable, ordered,
   side-by-side overlay inside the visible picture.
7. Overlay sizing follows output resolution, remains usable at 1080p and 4K,
   and retains compact internal spacing and consistent edge placement.
8. Profile display defaults to five seconds when omitted, includes the fade in
   that total, and suppresses the overlay when set to 0/Off.
9. Saving Profile display applies the new duration live without rebuilding
   capture or the renderer.
10. Existing profile shortcuts, renderer selection, refresh switching, and
    supported action conditions continue to pass their regression suites.

## Implementation evidence

- Config exposes dedicated top-level renderer tabs and independent profile
  lists for Color Config and Screen Config while preserving the original
  Rendering page.
- `UnifiedProfileRuntime` publishes the committed effective selections and
  friendly color/screen aliases from one snapshot.
- `EventActionLauncher` provides per-group delayed newest-trigger-wins
  scheduling with explicit lifecycle diagnostics.
- `ProfileChangeOverlay` collects, orders, and formats the changed profile
  groups; the native renderer overlay path renders them within the visible
  output region.
- `profile_change_display_seconds` is validated from 0 through 60, defaults to
  5, is documented in the public configuration inventory, and uses a dedicated
  live display-apply classification.

## Validation and deployment evidence

- x64 Release builds succeeded for the VideoProcessor GUI and Config editor;
  the shared renderer/library changes compiled as part of the Release build.
- The complete native suite passed 877/877 tests after correcting the checked-in
  Color Config shortcut expectations and public configuration-field inventory.
- The complete Config UI suite passed, including five-second default, Off/0
  persistence, reload, tab layout, profile lifecycle, and live-apply behavior.
- The tested Release executables and accompanying renderer changes were
  deployed to `C:\Videoprocessor\vp`; the active user configuration was
  preserved. The final Profile display executable backup is under
  `C:\Videoprocessor\vp\backups\profile-display-label-20260826-191055`.
- The developer exercised Rec.709/BT.2020, screen, rendering, and lamp-only
  transitions on the projector and confirmed the resulting workflow and OSD
  work well.

## Integration boundary

The projector-specific `C:\Videoprocessor\utils\set_hdr.bat` and
`C:\Videoprocessor\lamp` scripts were adjusted during deployment validation to
add command logging, replace stale six-character `.fingerprint` markers, exit
after the first matching state, and retain the lamp-change settling delay.
Those local scripts prove the action contract but are not portable
VideoProcessor repository functionality and are therefore not part of the
merged source change.

## September 20 local action-convergence correction

The live 11:18:03-11:18:09 sequence showed F2 Scope accepted while a 16:9
script ran, but the August 30 broad feedback guard discarded its actions.
set_hdr consequently read screen/169.state and chose Rec709 169 Med instead
of Scope Med (113 versus 117 nits in the user's configuration).

Candidate 3d55d6f9d559b38d9f6cfc11a8751c81eba42583 on
codex/profile-action-latest starts from freshly fetched current beta a6914dc6,
including the latest crop fixes. Rendering-only manual shortcut feedback is now
classified at the originating selection, preserving generic callback protection.
Independent screen/color/zoom/queue and mixed selections retain their delayed
serialized actions; latest pending batches win. Debounce tokens no longer reset
after a claim or cancellation, preventing old sleeping workers claiming new work.
The retained compatibility policy still suppresses action callbacks for a manual
Rendering-only shortcut during a running script; it does not identify key origin.

RED: screen eligibility plus two stale-token cases failed; feedback control passed.
Final clean committed x64 Release build: 1381/1381 tests passed, including seven
new regressions and a real profile-runtime three-cycle fixture with generic
state.committed callbacks. Source remains local, not pushed or merged.

Deployed the paired host/renderer at 11:57:58, PID 16856. Both clean startup
identities and deployed hashes match. Configuration and external scripts were
unchanged. A live shortcut check was interrupted by application shutdown at
11:58:20; do not claim live convergence verified. VP was left closed for user testing.
State was backed up before the shortcut check, which selected Scope during shutdown.

Rollback binaries and pre-test profile/marker state:
C:\Videoprocessor\vp\backups\profile-actions-3d55d6f9-20260920-115758.
Evidence, deployment receipt and tests:
C:\Users\bslac\Documents\ChatGPT\Done\profile-action-latest-20260920.
Source design: docs/PROFILE_ACTION_LATEST_SELECTION.md.
This follow-up records a local trial; the original story's historical Done status
is unchanged. User validation: F3, then F2 about 1-2 seconds later; allow scripts
to settle and verify Scope plus Rec709 Scope Med and screen/235.state agree.

### September 20 beta merge

User authorized merge. PR #104 merged into v1.3.005-beta as
950e0a321bf44d903125922e4479650d9269b49e:
https://github.com/billslack2/videoprocessor/pull/104.
The pinned feature was 3d55d6f9d559b38d9f6cfc11a8751c81eba42583 against
unchanged beta a6914dc6e09ce95b4c9373f17ea9a5a74e0612ec; the release merge
helper verified identities and applicable checks. Clean Release evidence remains
1381/1381 passing tests. Existing local deployment is the tested feature commit;
no new deployment or release packaging was requested or performed for this merge.
