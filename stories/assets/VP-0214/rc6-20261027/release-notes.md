# VideoProcessor 1.4 RC6

This is a separate **beta prerelease**, version **1.4.0-rc6**.

- Improves subtitle continuity during dark scenes when shared picture evidence becomes unavailable, while retaining current-pixel and boundary checks.
- Preserves rounded subtitle backing coordinates through brief evidence gaps and requires fresh pixel proof before cleanup resumes.
- Includes complete subtitle-picture handling for Smooth Motion, bounded recovery across brief gaps, and additional Clear/Yellow + Float coverage.
- Keeps fullscreen/windowed occlusion and zero-area presentation pauses from incorrectly marking the renderer as failed.
- Adds focused subtitle/backing diagnostics for remaining intermittent cases.

Download the full Setup EXE or x64 Portable ZIP. Both include VP, both renderers, Config and the updater. There is no separate Config-only package. For a fresh portable folder, copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`; preserve existing settings when upgrading. Select the Beta update channel for automatic release-candidate updates.

Windows 10/11 x64. Windows binaries and installer are unsigned; updater metadata is signed. Clean Windows without Visual Studio and physical display/capture acceptance are not newly qualified for this release. Some dark-scene boundary transitions and fullscreen aspect-ratio history transfer remain under investigation; this release does not claim every subtitle/AR flicker is resolved. Older versions may not understand newer settings when rolling back.

Application source matches merged beta `3bae9f228173b9b2d024d7c977a87766bcbc02c1` ([PR #221](https://github.com/billslack2/videoprocessor/pull/221)). Release source `e3a34b6c9358485e2d975d7b76c7be1ad05baec2` carries forward existing packaging fixes and corrects two stale test expectations for the already-existing two-row attached-panel sampling overlap. Production application sources are unchanged from the merged beta.

Installer/updater qualification: 225 isolated actual-installer lifecycle checks passed with matching managed payload hashes; 29 updater policy checks, 19 control checks, 8 portable configuration contract checks, both portable update transactions, and native Config update-scope/shutdown tests passed. These lifecycle checks ran on the development machine with a separate registration and shortcuts, not a clean Windows VM.



Qualification limitation: one full run reported a same-size/restored-timestamp configuration-cache invalidation failure. All five cache tests and 20 additional repetitions of the failing case passed on unchanged binaries. The failure is preserved for follow-up; these reruns do not establish a fix for that intermittent behavior. Subtitle regressions passed in that run.

Final qualification on unchanged release binaries: **3,481/3,481 tests passed**, followed by installer-support, installer-identity, runtime-packaging, payload-hash, and finished portable-configuration checks. The final isolated installer lifecycle recheck also passed all 225 checks.
