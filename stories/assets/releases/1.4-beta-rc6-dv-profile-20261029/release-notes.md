# VideoProcessor 1.4 RC6

**The DV fixes in this release are untested on real capture hardware.** Automated coverage is included, but live DeckLink/Magewell Dolby Vision validation is still pending.

- Fix missing green active indicators and incorrect Not active labels for DV, Motion and other profile groups. Profile activation/status now shares one group definition, handles stale status, and preserves the profile being edited.
- Extend TV-led Dolby Vision tunnel decoding to byte-preserving ARGB8/BGRA8 capture alongside R210. This handles supported RGB 4:4:4 input that a capture SDK exposes as an 8-bit ARGB/BGRA buffer.
- Add Magewell DV capture handling that preserves RGB tunnel bytes, verifies temporary input-range overrides, and restores the original setting on exit, signal loss and failures.
- Correct limited-range SDR fallback and decoded-picture analysis for these capture paths. Unknown fallback range is rejected.
- Add synthetic decoder, capture-SDK failure/recovery, range, metadata and renderer integration regressions. Host and both renderer plugins carry the matching ABI 31.

This beta prerelease updates RC6 with smoother NLS and NLS+ profile switching. Previously prepared exact pipelines are reused when returning to a profile, avoiding a repeated warmup and safe-fit flash. First use or a changed rendering contract still prepares in the background.

RC6 also improves subtitle continuity during dark scenes and Smooth Motion, and treats fullscreen/windowed occlusion and zero-area presentation as pauses rather than renderer failures.

Download the updated [Setup EXE](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc6/VideoProcessorSetup-1.4.0-rc6-d5607e080537.exe) or [x64 Portable ZIP](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc6/VideoProcessor-1.4.0-rc6-d5607e080537-x64-Portable.zip). Previous RC6 assets remain available below for rollback.

Windows 10/11 x64. Full Setup and portable ZIP include VP, both renderers, Config and the updater; there is no separate Config-only package. For a fresh portable folder, copy VideoProcessor.cfg.example to VideoProcessor.cfg. Preserve existing configuration on upgrades. Select the Beta update channel for automatic updates.

The Windows binaries and installer are unsigned; updater metadata is signed. Clean Windows without Visual Studio, interactive Config Apply/OK/reopen, cross-version rollback and physical display/capture acceptance are not newly qualified. Older versions may not understand newer settings on rollback.

Production application source matches beta c716bd8108b03b0ef279c14c584e097eecc29515, including profile PR #224 and DV PR #225. Release source d5607e080537ad2713e8e746b78226dda44162cb carries forward the published RC6 packaging corrections and two test-only subtitle expectation corrections.

The clean x64 Release rebuild passed all 3,504 native tests. Another 86 beta-DV/capture tests, including real ARGB/BGRA renderer integration, and nine targeted Config profile-status UI regressions passed. Installer support/identity/runtime, payload-hash and portable configuration checks passed; an isolated installer identity passed 225 lifecycle checks on the development machine. Updater verification passed 29 policy and 19 helper-control checks. Automated results do not qualify physical DV capture.
