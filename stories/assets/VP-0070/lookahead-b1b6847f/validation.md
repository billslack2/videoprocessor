# VP-0070 look-ahead trial — b1b6847f

Status: Blocked. This is a detection-only experiment, not accepted relocation.

Source: b1b6847fad4a0eec1dba790fe024999e7cdc153f, codex/subtitle-lookahead.
Base: current beta at start of work, v1.3.005-beta / c84863eaa59e03fe218de824c34b7c0220c4fd87.
The existing subtitle-moving branch/worktree was preserved. No beta merge.

Uses current plus up to seven available queued frames, with cached observations
and no additional queue wait. Each frame supplies its own bar evidence. A current
bar-anchored line is required; future-only text cannot start a box on blank input.
Both line orders are supported; newly acquired lines need corroboration when
future frames are available. Bounds stay tied to an acquisition reference;
format/bar/context changes invalidate matching. Cadence repeats reuse results.
Centered geometry rejects isolated corner titles while retaining short cues.
The named Rendering-profile diagnostic setting survives Config save/reopen.

Final validation:
- Clean pinned x64 Release solution rebuild succeeded.
- Native focused subtitle tests: 50/50; full native suite: 1,935/1,935.
- Final Config suite: exit 0, 82 PASS, 0 FAIL. Physical two-monitor placement
  skipped because only one monitor was available; synthetic placement ran.
- Independent implementation review completed with no outstanding blockers.
- Exact staged configuration accepted by production ConfigFile and
  RendererProfileConfig::Read; root subtitle_bbox_test enabled. Config SHA256
  5BF8CD06E6C5041B62E8FBDF282B262077C632BE5EB7130E80C9E8E4F9A15EA1 matches prior trial.
- Isolated host /help exited 0. This is not a playback test.

Spatial corpus: antialiased synthetic captions over six real movie backgrounds;
558 frames per resolution. All 360 negative/gap frames had zero boxes at HD/UHD.
All 198 positive frames had a detection, but complete bounds were 168/198 at HD
and 177/198 at UHD. The stronger upper crossing two-line case was complete only
9/18 and 12/18 respectively; bottom two-line 12/18 at both. A continuous cue over
changing scenery produced three HD/four UHD boxes. These are spatial tests, not
end-to-end queue/presentation tests. Busy picture detail still merges with or
obscures glyphs, so complete early bounds and stability are NOT established.
Hi was complete 18/18 at both sizes; UHD area ratio 1.509 fails the fixture's
1.5 tightness threshold due to normal padding, not missing glyphs.
Detector-only median/p95: HD 1.70/2.78 ms; UHD 2.59/3.90 ms. These exclude the
whole renderer/bar-analysis pipeline and do not qualify live frame-budget cost.

Test folder: E:\codex\subtitle-lookahead-test-b1b6847f.
Close existing VP, then run START-SUBTITLE-TEST.cmd here; use the 16:9 viewport.
Host and renderer match the pinned Release artifacts; original runtime sidecars
and verified app-local Microsoft runtime dependencies are included in TEST-COPY.json.
Previous isolated trial config/state copied unchanged, with timestamped copies
under backups/. No configuration edits, usual install changes, or playback launch.
Backups: VideoProcessor.cfg.before-subtitle-lookahead-20261002-205707 and
VideoProcessor.state.before-subtitle-lookahead-20261002-205707.

Build/test logs and TRX: E:\codex\videoprocessor\subtitle-lookahead\subtitle-final-*.log
and TestResults\subtitle-final-{focused,full}.trx.
Scope remains Step 1: bright green box only, no OCR, extraction, erasure or moving.
VP-0070 remains Blocked pending representative live validation and spatial fixes.
