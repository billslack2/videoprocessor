# VP-0188: Preserve configuration scope and avoid source-event disk reloads

## Status

Review — implementation and automated validation complete 2026-09-15; active-signal hardware acceptance remains pending. Source branch
`codex/vp-0188-config-scope-reloads`, worktree
`E:\codex\videoprocessor\assess-config-feedback-20260915`, based on current
`origin/v1.3.005-beta` at `455d919c`.

## Problem

1. The configuration editor writes shared `switch_refresh_rate` to [general],
   while the legacy renderer validator warns that this generated key belongs in
   [vpvr.general]. A clean generated configuration must not warn about itself.
2. Opening or saving a DirectShow-only input policy promotes it to [general].
   General video conversion has no unset choice. VP Renderer also falls back to
   DirectShow, allowing a backend-specific choice to affect another backend.
   The same migration covers container color space, HDR color space/luminance.
3. A renderer source-state update loads VideoProcessor.cfg to inspect its format
   and loads it again to resolve settings. HDR metadata withdrawal/restoration
   can cause four reads per blip despite an unchanged file and continuous signal.

## Scope and intended behavior

- Keep refresh-rate shared policy canonical in [general]; align warning and
  compatibility behavior, preserving genuine legacy policy warnings.
- Preserve explicitly scoped input settings through load/save/reopen. Allow the
  shared conversion setting to be unset. Backend defaults resolve from their own
  section, shared General/command-line values, then defaults, without a DirectShow
  value leaking into VP Renderer. Document the compatibility change.
- Resolve automatic source/metadata updates against the renderer's accepted
  configuration snapshot. Explicit configuration application refreshes the
  snapshot; manual profile selection uses accepted configuration consistently.
- Add focused diagnostic evidence for configuration snapshot generation/read
  versus source-state reuse, without per-frame log noise.
- Preserve live metadata propagation, automatic profile rules, explicit reloads,
  and rollback when a configuration change is rejected.

## Acceptance and tests

1. Fresh editor config and save/reopen do not emit the shared refresh warning;
   legacy renderer-only misplaced settings still warn appropriately.
2. DirectShow-only input settings remain DirectShow-only after save/reopen;
   General can be unset, Disabled remains an explicit override, and VP Renderer
   inherits only shared settings when its own setting is unset. Cover mixed and
   legacy renderer aliases and all four input-policy keys.
3. Metadata withdrawal/restoration and automatic profile evaluation perform no
   extra configuration disk reads after snapshot acceptance. Profile shortcuts
   and explicit saved configuration application continue to work; a rejected
   candidate does not replace accepted settings.
4. Execute focused unit tests, full relevant core/config-editor suites, and a
   successful x64 Release build; record commands and results here.
5. Automate fresh-file editor/runtime checks where possible. Before hardware
   testing, ask the user to confirm an active capture signal. Capture baseline,
   repeated HDR metadata events, profile changes, and a saved config edit with
   timestamped logs; verify read/reuse counts and lack of unwanted resets.
6. If source events cannot be generated automatically, provide a concise manual
   checklist and log location, then inspect returned/local logs before claiming
   hardware acceptance. Keep story in Review until this evidence is accepted.

## Progress

- Remote beta verified; tracker ID audit: 206 files/rows, no duplicate or missing
  IDs, maximum root VP-0187. Assigned VP-0188.
- Assessment confirmed the save/load promotion, cross-backend fallback, missing
  unset option, warning mismatch, and duplicate renderer loads.

## Implementation and validation evidence (2026-09-15)

- Source commit: `c456c325a163485d9af2d598cfe0abaff62f3446`.
- Draft PR: https://github.com/billslack2/videoprocessor/pull/93
  against `v1.3.005-beta`. Not merged; ready for implementation review and hardware acceptance.
- Removed DirectShow-to-General load/save promotion and UI fallback, removed the
  startup shared DirectShow fallback and VP Renderer fallback, and added General
  conversion Not set. A shared resolver covers all four backend input fields and
  preserves supported renderer aliases. Clearing a previously configured input
  restores its default without resetting unrelated command-line/session inputs.
- Shared refresh policy remains canonical in General; warning and precedence
  agree. Genuine misplaced renderer-only settings still warn.
- Parsed configuration is retained in immutable runtime snapshots. Renderer
  construction/source updates/profile evaluation use the accepted snapshot;
  explicit host reload replaces it, while rejection retains the old renderer
  snapshot. ABI bumped to 20 because Snapshot crosses the paired DLL boundary.
- Configuration disk reads and renderer snapshot acceptance/reuse have distinct
  log records. `tools/verify_config_event_log.ps1` reports counts for a selected
  interval and rejects insufficient source events or unexpected reads.

### Executed checks

- Full x64 Release solution rebuild: PASS (repository v142/v143 toolsets).
  Final committed-source build: PASS, zero errors, four existing toolchain warnings.
- Core VSTest: **1,151 / 1,151 passed** (49.4 seconds).
- Qt editor widget suite: **71 / 71 passed**. Physical two-monitor placement was
  unavailable; synthetic negative-origin geometry and live HWND checks ran.
  An earlier foreground/topmost timing failure passed in the final full run;
  no topmost implementation was changed in this story.
- Actual renderer DLL integration: PASS. D3D initialized in a hidden test window;
  ten synthetic PQ/BT.2020 HDR metadata withdrawal/restoration cycles, F4 profile
  selection, and an explicit accepted host reload completed. The on-disk file was
  deliberately changed before source updates to verify snapshot isolation.
- Log verifier: **20 source updates (10 absent / 10 present), zero renderer disk
  reads, zero source-driven rebuild requests, three snapshot acceptances**.
- Core rejected-reload test preserves the accepted configuration; editor tests
  verify fresh config warnings, scoped save/reopen, and clearing General.
- `git diff --check`: PASS.

### Local artifacts

Root: `E:\codex\videoprocessor\assess-config-feedback-20260915`.

- `artifacts\vp0188-evidence\`: complete core/editor logs, TRX files, committed
  build log, real renderer integration log, verifier JSON, and package hashes.
- `artifacts\vp0188-test\`: isolated Release package, 58 immutable files verified
  by the release packaging script. The source-example config is only an example;
  no active user configuration has been copied over or modified.
- Packaged `VideoProcessor.exe` matches the built `VideoProcessor-GUI.exe` SHA256:
  `171E137B9361A93C2EE47A4AC2C859FEDCAA4448B09DA32722D59C591025DD45`.
- Packaged `vprenderer\VideoProcessorVPRenderer.dll` matches build SHA256:
  `67BB87F56838D70D0AEE803D8E46A00D4095702E87ADB6A12EED1CACE03EC415`.

### Live Apple TV validation — 2026-09-15

User confirmed the Apple TV signal. The isolated paired Release candidate captured
3840x2160 SDR/Rec.709 at about 59.941 Hz from 19:31:22 to 19:38:31 EDT.
Refresh policy emitted no deprecated-General warnings; the VP-specific NONE input
conversion override remained active despite shared/DirectShow P010 values.
Q and Ctrl+Q changed/restored display profiles live with the same accepted config
identity, one existing host read per shortcut, and no renderer swapchain rebuild.
After controls/editor opening, log lines 560–1098 contained zero configuration reads.

No actual HDR metadata loss/restoration occurred: this proves SDR capture and live
profile handling, not the HDR-blip regression. Visual continuity was not verified;
screenshot capture and mouse geometry were unavailable. Editor Apply was not tested
live (no changes saved); its automated widget tests passed as recorded above.

The copied config needed one beta-compatibility adjustment: comment development-only
viewport screen_edge_padding=50, rejected at initial startup. Backups preserved.
External actions disabled and logging enabled only in the isolated copy. Installed
configuration hash unchanged; installed VP restored after clean candidate shutdown.
No deployment performed.

Evidence: artifacts/vp0188-evidence/apple-tv-live-report.md,
apple-tv-live-20260915.log, apple-tv-live-test.cfg, and
apple-tv-live-verification.json in the source worktree above.

### Remaining acceptance / next action

Remain in Review. Request actual HDR Apple TV content/menu, verify logged absent/
present metadata updates with zero source-triggered reads, and obtain visible
picture-continuity observations. Complete live editor clear/save/reopen/Apply and
Blu-ray transition checks using docs/VP-0188-validation.md. Retain logs and inspect
them before marking hardware acceptance or moving this story Done.
