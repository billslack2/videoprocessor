# VP-0188: Preserve configuration scope and avoid source-event disk reloads

## Status

Review — corrected build deployed and replacement QA ZIP generated 2026-09-15.
Public PR #93 head `3acd02b3` on `codex/vp-0188-config-scope-reloads` includes
GitHub-verified beta `v1.3.005-beta` at `94f938d33216d0f212797f1ed4c6f83db02d9187`.
Corrected source worktree: `E:\codex\videoprocessor\vp0188-current-beta`.
Core tests: 1,161/1,161 passed; two editor runs each passed 74/75 with unresolved
intermittent foreground failures. Physical metadata-blip/HDMI QA and UI focus
qualification remain pending. The previous `ddfca7ab` ZIP is superseded; use `3acd02b3`.
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

### HDR/SDR switching follow-up — 2026-09-15 20:23–20:26 EDT

User confirmed HDR ready and manually switched between HDR and SDR; user reported
that the picture looked fine. Candidate logged PQ/BT.2020 HDR at approximately
23.976 fps, SDR/Rec.709, then HDR again. These transitions included invalid capture
state and full HDMI format resync, not metadata-only updates with continuous signal.

Measured reads: 23 during startup, then 28 across three full resyncs (8, 10, 10),
51 total in this session. All read the same content identity 1971779340220525956.
Zero further reads from 20:24:25 through clean shutdown at 20:26:47 (log lines
2245–2529); zero metadata-only updates throughout the session. Thus picture recovery
passed user observation, but metadata-only hardware acceptance is still unproven.
This run was shorter than seven minutes and is not recorded as a completed HDR-menu soak.

Remaining efficiency gap, raised by user: full renderer construction still calls
StageSavedConfiguration("renderer-lifecycle", true), performing repeated stable-file
reads and shortcut staging; LibplaceboPluginVideoRenderer separately loads config to
resolve its path, and other renderer/shader setup paths load again. The existing
VP-0188 patch only eliminates source-metadata snapshot evaluation reads, not all
startup/full-resync reads. Do not describe 51 reads as optimized or required.
A follow-up change should share accepted configuration through unchanged-file
renderer restarts while retaining explicit Apply/reload, legacy override handling,
and rejected-file protection. Add end-to-end host restart read-count coverage;
the existing synthetic renderer test does not cover host restart construction.

Evidence in artifacts/vp0188-evidence: apple-tv-hdr-switches-20260915.log (includes
preceding SDR session; select timestamps above), apple-tv-hdr-settled-verification.json.
Installed VP restored afterward; no deployment or production config edits. Story
remains Review; the full-resync inefficiency is documented and not yet implemented.

### Restart-read implementation and QA package — 2026-09-15

The previously documented full-restart read gap is now addressed by source commit
`ddfca7abdd8482e8dde09becafcf3130e6ec3b8f` on the same PR #93 branch/base.
ConfigFile keeps a bounded per-module parsed-file cache, checking Windows identity,
size, last-write time and change time before reuse. Replacement/deletion/recreation
and same-size edits invalidate it. Explicit Apply/reload still forces independent
fresh stability samples and retains schema/last-known-good validation. File metadata
probes remain; the measured reduction is configuration content reads/parses.

- Clean x64 Release rebuild: PASS. Incremental LNK1103 debug-info corruption was
  resolved by clean rebuilding; committed-source Release build also passed.
- Core: **1,155/1,155 PASS**; editor: **71/71 PASS**. Four new tests exercise reuse,
  force-fresh, alias/warning preservation and file invalidation, including a
  same-size edit with restored last-write timestamp. Renderer HDR-cycle test passed.
- Live DeckLink/Apple TV PQ/BT.2020 HDR capture: **two startup content reads total**
  (one host including pre-log read, one renderer), versus 23 previously logged.
- Full renderer restarts via Shift+R at 20:41:55, 20:42:23 and 20:42:51 EDT:
  **zero further reads across all three**, with resumed presentation telemetry.
  Verified log interval lines 669 through shutdown at 20:45:39; no deliberate
  configuration edits in that interval.
- Explicit host Apply through the editor's existing ConfigurationChanged.v1 IPC:
  duration 7→6 accepted at 20:40:32; invalid value rejected/accepted state retained
  at 20:40:52; backed-up value 7 restored at 20:41:14. Two fresh host reads each.
  This is host Apply validation; widget tests cover the editor controls.
- No new-build user-driven HDMI format-switch repetition or metadata-only blips
  were observed in this session; these remain on the QA checklist. Do not equate
  the automated full renderer restart with a physical HDMI resync or claim visual
  acceptance without tester observation.

Local evidence: artifacts/vp0188-evidence/restart-cache-report.md, live log/verifier
JSON, build logs, core TRX/log, editor log, paired hashes, qa-zip-verification.json.
No deployment/config overwrite. Isolated candidate shut down cleanly; installed
VP had already been stopped before testing and was left unchanged.

QA archive (usual Documents/ChatGPT/Done folder):
`VideoProcessor-v1.3.005-beta-VP0188-ddfca7ab-x64-Release.zip`
29,784,699 bytes; 62 files including QA docs, log verifier and internal hashes.
SHA256: `0f985f9870a343ab9e47eb2356d3352aeebdc27038fa7f5435e2385eeb13d256`.
Archive CRC and every staged file hash verified. Active config/state/logs excluded.
Companion `-QA.md` and `.sha256` files created. Paired host/renderer remain together.

Status remains **Review**, pending QA with this ZIP. User explicitly authorized
publishing the tested source commit and updating the existing public draft PR/story.

### User deployment — 2026-09-15 21:13 EDT

User explicitly requested deployment. Installed tested x64 Release commit ddfca7ab
at C:\Videoprocessor\vp. Replaced six changed manifest files: host EXE, renderer DLL,
config editor EXE/discovery DLL, configuration HTML, and release manifest. Verified
host SHA256 d770e5f6e4c84faee4a152912749de971667d32d7846d3f55bd89471414e7420
and renderer SHA256 7aecf82ec90830c296b25fbf9eece3a529c51697ab816941a3e9f4fb0374e574
against the tested Release package. Unchanged dependencies reused.

Backup: C:\Videoprocessor\vp\backups\VP0188-ddfca7ab-20260915-211330.
Backed up every replaced file plus VideoProcessor.cfg and VideoProcessor.state.
Only configuration edit: commented the exact line screen_edge_padding: 50 with
'# VP0188 beta compatibility: ' because the beta baseline rejects that development
key. All other configuration bytes preserved; existing shaders/state retained.

Launched installed build; API-20 plugin loaded and live HDR capture/rendering at
approximately 23.976 fps confirmed in logs. Startup host content-read counter=1.
Complete file/hash/backup audit: artifacts/vp0188-evidence/deployment-ddfca7ab.json.
Story remains Review for QA; deployment is not a claim of completed metadata-blip
hardware acceptance.


### Baseline correction, replacement ZIP and deployment — 2026-09-15

The ddfca7ab ZIP/deployment used stale beta tracking ref 455d919c and omitted
already merged VP-0186 screen-edge padding and REQ006 changes. This was an agent
baseline-verification error, not an obsolete user setting. remote.origin.fetch
listed main and two feature branches but omitted beta, so plain fetch left the
tracking ref stale. GitHub current beta was verified as 94f938d33216d0f212797f1ed4c6f83db02d9187
and explicitly fetched. Earlier statements implying the setting was unsupported
by current beta were wrong. Old ZIP/checksum/checklist renamed SUPERSEDED-DO-NOT-USE.

Restored prior installed files and the exact screen_edge_padding: 50 line first.
Created clean worktree E:\codex\videoprocessor\vp0188-current-beta from remote tip
94f938d3 and merged VP-0188 changes (f7bd0a74), then added padding snapshot regression
coverage and baseline-verification documentation in 3acd02b3. Remote beta ancestry
verified again immediately before packaging. BUILD-INFO.json in the ZIP records
both full hashes. No force push is required; corrected head includes previous PR head.

Validation:
- Clean x64 Release build PASS. Incremental debug-info linker corruption resolved
  by clean rebuilding, with no deployment of failed-build outputs.
- Core: 1,161/1,161 PASS, including CurrentBetaScreenEdgePaddingSurvivesAcceptedConfiguration.
- Editor: two complete 75-case runs, 74 PASS in each. Run 1 failed external
  foreground/topmost; run 2 passed that but failed stable reveal foreground retention.
  Each failed case passed in the other full run. Isolated topmost retries failed;
  a pre-existing remote-beta build passed its comparison run. This is unresolved
  intermittent UI foreground timing, NOT a clean editor-suite pass. Configuration,
  calibration, and scope cases passed in both. All logs retained; caveat in QA notes.

Corrected QA ZIP: VideoProcessor-v1.3.005-beta-VP0188-3acd02b3-x64-Release.zip
in Documents/ChatGPT/Done; 29,820,471 bytes, 64 files. All archive hashes/CRC verified.
SHA256: 35362d02e33e8a724778b380c58953c8f5c1ce980005d40549058b34509a3ce3.
Do not use superseded ddfca7ab ZIP. Corrected archive contains padding/REQ006 features,
QA checklist, log verifier, file checksums and explicit verified baseline provenance.

Corrected build deployed at C:\Videoprocessor\vp and launched. Backup:
C:\Videoprocessor\vp\backups\VP0188-3acd02b3-20260915-213608.
Configuration restored byte-for-byte to pre-incorrect-deployment SHA256
77e0fac0f279b6813f5fef195ca6f042d47822d32b1b99bc8cc451f607a5408d.
No configuration edit in corrected deployment. Existing state/shaders retained.
Installed host SHA256: 07e9857ce9b53c669bdec9b48e795a4d1de55c010095e68e461b4c59e559f76b.
Installed renderer SHA256: b874bf5869de3691d000173fc69a3378fcb7800c48a43a46def3c2e8f40dd9fe.
Paired hashes match corrected ZIP. Live HDR ~23.976 fps confirmed. Logs show
screen_edge_padding_requested=50 and effective=0 for center alignment, as intended.
Startup content reads: one host plus one renderer. Hardware metadata-blip/HDMI QA
and UI focus qualification remain pending; status stays Review.

Evidence in corrected worktree artifacts/vp0188-evidence: build/test logs, core TRX,
BUILD-INFO.json, qa-zip-verification.json, deployment-3acd02b3.json,
corrected-deployment-live.log. User explicitly authorized correction publication. Commit 3acd02b3 pushed to public PR #93; PR description updated with corrected baseline, replacement ZIP and validation caveats.
