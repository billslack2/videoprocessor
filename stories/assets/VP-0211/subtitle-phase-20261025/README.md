# Subtitle transition cadence recovery, 2026-10-25

The earlier NLS issue was real and the user reported it fixed. The remaining
failure is triggered by subtitle profile changes, notably clear/yellow, at both
24 fps and 60 fps; switching NLS off does not clear it. It may recover after
seconds or minutes. The user accepts a brief internal recovery without an HDMI
resync. The precision-only candidate was insufficient.

The preserved incident shows yellow selected at 14:21:06, then paired cached
repeats and source retirements with frames queued. Source phase leads of about
0.02–1.14 ms exceeded the old 10 µs allowance. Sample GPU timing was about 5–6 ms
at 24 Hz and 4 ms at 60 Hz; CPU rendering wall time includes driver pacing and
is not itself GPU shader time.

Feature commit: `6f6c0d2e58c2ea732bbe192ef5c7261d8df7dbbb`, draft PR #204.
Separate eligibility and retirement edges use a 3 ms band, capped at one quarter
of a refresh. Raw source PTS and playback speed stay unchanged. Tests preserve
24-to-60 3/2 cadence and genuine long-term rate corrections.

After an output change, one successful changed frame pays warmup, then internal
repeat timing, feedback and cache eligibility reset once. A failed present keeps
the recovery request pending. This requests no display-mode change. Subtitle
pixels, NLS rendering and source Queue/lookahead are unchanged.

Validation:

- Clean feature x64 Release build passed with 11 warnings and zero errors.
- All 20 clock/transition tests passed, including ±1.14 ms phase jitter at
  24/59.94/60 Hz, conservative retirement, 24-to-60 phase, actual rate drift,
  and coalesced recovery after one successful present.
- Combined commit `621223092dbea7e088947523a1c9f260c4bdb930` preserves the installed
  tone GUI sources. Its clean x64 Release build passed with 11 warnings and
  zero errors, and all 425 focused compatibility tests passed.
- Luna xhigh reviewers examined regressions and mpv primary sources. Final code
  review found no concrete blocker. Full production transition ordering was
  reviewed in source; the helper test is not a complete RenderLoop integration.
- Earlier 429/421 test passes remain historical evidence for unchanged pixel
  paths and are not counted as new passes for this candidate.

Deployment and live observations:

- Matching Release host, both renderers and receipts were hash-verified.
- Configuration was backed up and preserved byte-for-byte: every-refresh on,
  depth 3, Motion off. The installed tone GUI and Config session were preserved.
- Backup paths and hashes are in deployment.json. Graceful shutdown succeeded;
  responsive PID 82768 restarted with matching clean host/renderer identity.
- Fresh logs show the one-time internal restart with hdmi_resync=0.
- Subsequent user-driven reconstruction, clear/yellow and black subtitle changes
  at 14:40:51–14:40:54, and NLS changes through 14:41:04, each produced a cadence
  restart. No subsequent cached-repeat log entry or renderer-drop growth was
  observed in the sampled window. This is software evidence, not visual acceptance.

Live smoothness, subtitle placement and repeated transitions at 24/60 Hz still
need user/hardware confirmation. VP-0211 remains Review; PR #204 remains draft.
No merge or public release occurred.

Rollback: stop VP, restore the host, both renderer DLLs and their matching runtime
receipts from the backup recorded in deployment.json, then restart from
C:\Videoprocessor\vp. Preserve current configuration unless the user requests
settings rollback. No configuration edits were made for this deployment.
