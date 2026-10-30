# VideoProcessor 1.4 RC6 - DeckLink EDID update

This RC6 refresh is published on the **Stable** update channel. It adds the documented DeckLink EDID workaround and includes the latest merged renderer and Config changes: background tone-chart preparation keeps controls responsive, and the Beta renderer now uses the rebased libplacebo API 388 fork with upstream gamut LUT encoding fixes. The standard renderer dependency remains 7.360.1; experimental gamut correction remains Off by default.

## DeckLink Dolby Vision

The optional [EDID BIN](https://raw.githubusercontent.com/billslack2/videoprocessor/605ec4af39b934ac4bbd0f3e7dbbe7e31bec7e63/docs/edid/vertex2-lg-dv30-hdr10bit.bin) ([Vertex2 installation guide](https://github.com/billslack2/videoprocessor/blob/605ec4af39b934ac4bbd0f3e7dbbe7e31bec7e63/docs/edid/README.md)) preserves low-rate 4K Dolby Vision while withdrawing high-rate 4K DV and 12-bit deep-color advertisements. Installing VP does not change the Vertex2 EDID; apply it separately only to a compatible setup, after backing up the existing EDID.

On the tested DeckLink Quad HDMI Recorder, seven RGB capture formats failed to deliver valid input at UHD50 and UHD59.94. YUV capture delivered frames but did not preserve the standard Dolby Vision RGB tunnel. This EDID avoids negotiating that unsupported high-rate mode; it does not add a new high-rate DeckLink DV decoder.

With Apple TV, Vertex2 firmware 0.60 and Desktop Video 14.4.1, the final EDID was read back byte-for-byte. User playback confirmed **Our Oceans at UHD25 Dolby Vision** and **Meridian at UHD59.94 HDR10**, with a correctly rendered picture. True 50-fps HDR fallback after the change remains unverified; exact 60 Hz was capability-tested only. Fallback depends on the source/app's available renditions. HDMI transport rate and content frame rate are different: these results do not establish the title's mastering rate or imply that unique frames were discarded.

## Packages

Windows 10/11 x64. Full Setup and the portable ZIP include VP, both renderers, Config and the updater. Use the Stable update channel. Earlier RC6 downloads remain available for rollback.

Setup preserves existing settings. For a fresh portable folder only, copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`; preserve existing configuration when upgrading. This release does not change the user's live VP installation automatically.

Windows binaries and Setup are unsigned; automatic-update metadata is signed. Clean Windows without Visual Studio and interactive Config Apply/OK/reopen were not newly qualified. Hardware observations above were made during the EDID investigation, separately from this fresh release build.




## Downloads and qualification

[Setup EXE](https://github.com/billslack2/videoprocessor/releases/download/1.4-rc6-decklink-20261030/VideoProcessorSetup-1.4.0-rc6-605ec4af39b9.exe) | [Portable ZIP](https://github.com/billslack2/videoprocessor/releases/download/1.4-rc6-decklink-20261030/VideoProcessor-1.4.0-rc6-605ec4af39b9-x64-Portable.zip)

Fresh x64 Release build from merged beta commit 605ec4af39b934ac4bbd0f3e7dbbe7e31bec7e63. All 3538 native tests passed, together with installer/runtime/portable-configuration and updater-policy checks, six packaged-renderer tone-chart regressions, 19 updater-control checks, 326 isolated real-installer lifecycle checks, two portable-update fixtures and corrupt-package recovery. Installer lifecycle includes upgrade and rollback against the previous RC6. No live deployment was performed.

[EDID and packaging PR #232](https://github.com/billslack2/videoprocessor/pull/232) and [test-only RC6 corrections PR #233](https://github.com/billslack2/videoprocessor/pull/233) are merged. The latter keeps source cleanup boundary checks separate from the intentional two-row subtitle display-panel overlap; application behavior is unchanged.
