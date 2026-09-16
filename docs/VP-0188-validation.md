# VP-0188 validation

## Automated checks

Use the repository toolsets (v142 for the host/core and v143 for Qt), x64 Release:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe' VideoProcessor.sln /m /p:Configuration=Release /p:Platform=x64
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' x64\Release\VideoProcessor-Test.dll /ResultsDirectory:TestResults '/Logger:trx;LogFileName=vp0188-core.trx'
& .\x64\Release\VideoProcessorConfigTests.exe
```

The core suite includes `RendererMetadataCyclesReuseAcceptedSnapshotWithoutDiskReads`.
It loads the actual paired renderer DLL, initializes D3D in a hidden test window,
and supplies PQ/BT.2020 state without capture hardware. It changes the file on disk,
then exercises ten HDR metadata withdrawal/restoration cycles, F4 profile selection,
and an explicit accepted configuration reload. All source transitions must succeed
and the renderer log sink must observe zero disk reads. The host explicitly reads
the new configuration once for the reload. Output is
`x64/Release/vp0188-renderer-integration.log` and the VSTest TRX.
This verifies runtime state handling, not capture delivery or picture continuity.

The editor tests use real Qt widgets and disposable configurations. They cover
fresh-file creation without policy warnings, DirectShow-only save/reopen, clearing
General, explicit Disabled values, inherited labels, and mixed backend overrides.

## Signal-based acceptance (pending)

Confirm with the user that Apple TV or Blu-ray is supplying an active capture signal
before launching the candidate capture session. Use the paired x64 Release host,
renderer DLL, and config editor from one build. Use an isolated package and copies
of the active configuration/state/assets; preserve the deployed installation and
its settings. Do not change the deployment without a deployment request.

Record the candidate commit, source device, signal format, local start/end times,
and whether enhanced logging is enabled. Save a copy of the complete log before a
restart rotates it. Runtime logs are `logs/vp.log` beside the candidate executable;
for the installed application they are `C:\Videoprocessor\vp\logs\vp.log`.
Older sessions use `vp.log.0`, `vp.log.1`, etc.

### Manual checklist

1. **Fresh config:** in a disposable empty configuration, save and restart. Check
   refresh-rate choice persists and no warning directs the generated shared key
   from `[general]` to `[vpvr.general]`.
2. **Scope:** leave General conversion at Not set; choose V210 to P010 on DirectShow.
   Apply, close/reopen the editor, and restart VP. DirectShow retains the choice;
   General and VP Renderer remain unset/inherited. Repeat with explicit Disabled.
3. **Clear:** set General conversion to P010, Apply, return it to Not set, Apply.
   The shared key disappears and each backend resolves its own override/default.
   A VP Renderer-only override remains independent. Save/reopen once more.
4. **HDR menu:** with VP Renderer active, leave Apple TV on the menu where metadata
   blips occurred for at least seven minutes. Note the exact interval and any
   visible disturbance. The log must show actual HDR-absent/HDR-present source
   updates; time elapsed without any events is inconclusive. Those updates reuse
   one config identity and cause zero disk reads or metadata-only rebuilds.
5. **Profiles:** record the time, press a known configured profile shortcut, and
   confirm the visible profile change. Source-state/profile evaluation must keep
   using the accepted snapshot. Record the profile/key used.
6. **Explicit edit:** record the time, change a harmless live profile setting and
   Apply. Confirm the new value takes effect and the log records a new accepted
   configuration identity. Deliberate apply may perform host reads for stable-file
   validation; these must not be mistaken for source-event reads.
7. **Blu-ray:** repeat the source transition that previously caused trouble. Note
   whether the source actually loses signal or merely changes HDR metadata. Check
   normal persistent SDR/HDR transitions still take effect and no image is stuck.

Provide the source, test start/end times, observed result for each step, and keep the
log available locally. Codex should inspect the log before recording hardware acceptance.

## Log verification

`Configuration disk read:` means a successful disk read (module-local serial).
`Renderer source configuration reused:` records cached identity and HDR presence.
`Renderer configuration snapshot accepted:` records the profile generation and
accepted content identity. `libplacebo configuration snapshot evaluated:` is an
in-memory evaluation, not disk I/O. Do not count it as a config reload.

```powershell
# Automatic integration log: requires real source-state updates, not just an idle pass.
.\tools\verify_config_event_log.ps1 -LogPath .\x64\Release\vp0188-renderer-integration.log -MinimumSourceUpdates 20 -ExpectNoDiskReads
# Live interval: choose log line bounds after startup and before deliberate Apply.
.\tools\verify_config_event_log.ps1 -LogPath 'candidate\logs\vp.log' -FromLine 100 -ToLine 900 -MinimumSourceUpdates 2 -ExpectNoDiskReads
```

The checker prints counts and fails when the requested event minimum is missing
or the selected no-read interval contains a disk read. Examine surrounding capture,
profile, renderer lifecycle, and runtime-input-setting lines for causal attribution.
A nonzero general-policy warning may identify another legacy renderer-only key;
inspect the saved config rather than assuming every warning is refresh-related.

## Startup and HDMI-resync read reuse

`ConfigFile::Load` reuses up to 16 parsed files per module when Windows file
identity, size, last-write time, and change time still match. Each request checks
file metadata; these checks are not configuration-content reads. Replacement,
deletion, recreation, and same-size writes invalidate reuse. If revision metadata
is unavailable, the loader reads normally. Host and renderer DLL have separate
caches, so expect one initial content read in each for a single unchanged config.

Explicit host Apply/reload uses `ReadPolicy::Fresh` for both stability samples.
Renderer-lifecycle validation uses revision-checked reuse, retaining schema
validation and last-known-good handling. The cache stores parsed files, not an
acceptance decision. A cached invalid configuration must still fail validation.

The startup log `Configuration read counters: module=host startup_content_reads=N`
includes content reads before logging was initialized. Add that initial count to
subsequent `Configuration disk read:` events when comparing complete sessions.
After startup, repeat HDR/SDR switches that cause actual HDMI resyncs. Require
zero further reads while the configuration is unchanged; retain the entire log,
exact event interval, and the user's picture-recovery observations. Also verify
an explicit saved edit is picked up and a rejected edit retains accepted state.
