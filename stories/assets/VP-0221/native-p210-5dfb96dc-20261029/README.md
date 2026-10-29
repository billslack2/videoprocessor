# VP-0221 validation — 2026-10-29

Source: `5dfb96dc7c35b4ffa28543e851aa3150f5e5be8d`, clean x64 Release.
Beta base: `c716bd8108b03b0ef279c14c584e097eecc29515`.
PR: https://github.com/billslack2/videoprocessor/pull/226 (open; not merged).

- Complete Release solution build passed.
- 487 focused standard tests and 16 beta feature/integration checks passed.
- Both shipped GPU/caption suites passed: 208 with libplacebo 360 and 208 with 377. Per-run counts include overlapping beta focus cases; these are not 919 distinct methods.
- The installed Pro Capture HDMI 4K+ delivered native P210 at 3840x2160 SDR/59.941 Hz (8-bit YCbCr wire input) and HDR10/23.976 Hz (12-bit YCbCr PQ/BT.2020 wire input). Logs confirm `native_p210=1`, `repack=0`, direct P210 upload, valid HDR luminance, and identical live/queued analysis domains with 10-bit/shift-6 storage. User confirmed the SDR picture and native-capture OSD label.
- A content switch back to RGB8 BGRA DV validated CRCs, complete DM/L1 data and 12-bit 4:2:2 decoding; a 600-picture interval reported no rejected pictures or failed CRC copies.
- Matching host, both renderers, Config and ConfigDiscovery were deployed and all five hashes rechecked during validation. Backup: `C:\Videoprocessor\vp\backups\magewell-native-p210-5dfb96dc-20261029-110238`. No configuration edits were made.

A concurrent local stats build, `e6f8c62d3aeafc701ffdf9e6d3e399b29cd206ad`, superseded the host/renderers at 11:29 while final test evidence was being recorded. Git ancestry confirms it includes the tested native-P210 commit; its live log still reports native P210 without repacking. The newer installation was preserved. These tests qualify 5dfb96dc, not the additional stats changes. The summary distinguishes the originally verified hashes from the later observation.

The JSON files preserve source/dependency/binary identity, actual per-test results and selected hardware log evidence. Raw logs and TRX files remain under `E:\codex\diagnostics\magewell-native-p210-20261029`; local raw hardware logs are also in the deployment backup. No video pixels are stored here.

This removes two CPU transformations, not capture or GPU transfers. The existing 12-bit-input-to-10-bit-P210 precision limit remains. Other cards/drivers, the full format matrix, extended queue-pressure runs and calibrated image/performance qualification remain outside this check. GPU suites ran concurrently with playback, so no capture speedup measurement is claimed.
