# VP-0211 test deployment - 2026-10-25

User authorized deployment and startup for testing. Deployed the clean source
`727467e7230dae48315698087caead19518ca837` from its successfully completed x64
Release solution build. All five application/renderer/Config binary source
hashes matched their Release runtime receipts before copying; every destination
hash was verified after copying and again after startup. Both renderer dependency
sets already matched the build and were preserved. The updater worker was left
running; the hidden old Config endpoint was stopped before replacing its binary.

Backup and rollback source:
`C:\Videoprocessor\vp\backup-before-vp0211-20261025-084649`.
The backup preserves the earlier `7dc9af6` display-statistics test binaries,
matching runtime sidecars, Config/Discovery binaries, configuration help and the
active VideoProcessor.cfg. See deployment.json for exact file paths and hashes.
No PR was merged, and no installer or public release was produced.

Updated payload: VideoProcessor.exe, primary and Beta renderer DLLs,
config/VideoProcessorConfig.exe, config/VideoProcessorConfigDiscovery.dll,
CONFIGURATION.html and corresponding binary Release receipts.

Only active configuration edits, inserted into [general]:

```ini
present_every_vsync: true
presentation_queue_depth: 2
```

Removing exactly those two inserted lines reproduces the backup byte-for-byte.
All source Queue, lookahead, Motion, display-selection and other preferences
were preserved. In particular, the selected renderer remains VP Renderer (Beta)
and Smooth Motion remains auto. No source-example config was installed.

Started C:\Videoprocessor\vp\VideoProcessor.exe with working directory
C:\Videoprocessor\vp at 08:46:50 EDT, PID 27716. The process stayed responsive
and its fresh vp.log confirms the clean 727467e host and both plugin loads.
DeckLink capture started on the configured Quad HDMI Recorder (2), but no input
frames/renderer initialization were observed during this startup check.
Therefore the applied presentation-depth log, cached-repeat behavior, physical
cadence and end-to-end latency remain unqualified until an input signal runs.
Story remains Review. This checkpoint records deployment/startup, not live
picture acceptance.
