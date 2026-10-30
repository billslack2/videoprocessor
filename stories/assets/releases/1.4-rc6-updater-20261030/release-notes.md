# VideoProcessor 1.4 RC6 - DeckLink EDID and update-channel fix

This replaces the previous package on this RC6 release page with a small updater fix. It remains on the **Stable** channel.

- **Beta receives both Stable and Beta releases**, selecting the newest eligible signed release sequence.
- **Stable receives only Stable releases.** Existing downgrade/replay protection and package checks remain in place.
- Every release now includes **vertex2-lg-dv30-hdr10bit.bin** as a separate download. It is optional and applied manually to a compatible Vertex2; installing VP does not apply it.

**Updating an older updater:** select Stable and click Check now once, or download the new Setup below. After installing this fix, you can select Beta to receive both release types. An old Beta-only updater cannot acquire its new channel behavior until it is updated.

## Downloads

[Setup EXE](https://github.com/billslack2/videoprocessor/releases/download/1.4-rc6-decklink-20261030/VideoProcessorSetup-1.4.0-rc6-a8f62855c246.exe) | [Portable ZIP](https://github.com/billslack2/videoprocessor/releases/download/1.4-rc6-decklink-20261030/VideoProcessor-1.4.0-rc6-a8f62855c246-x64-Portable.zip) | [EDID BIN](https://github.com/billslack2/videoprocessor/releases/download/1.4-rc6-decklink-20261030/vertex2-lg-dv30-hdr10bit.bin)

[EDID installation guide and compatibility limits](https://github.com/billslack2/videoprocessor/blob/a8f62855c246019e3d9262f384e58c96d60f3147/docs/edid/README.md). The tested Apple TV / Vertex2 / DeckLink Quad HDMI Recorder setup retained UHD25 Dolby Vision and used UHD59.94 HDR10 for Meridian. True UHD50 fallback after the EDID change remains unverified; exact60 was capability-tested only. The EDID avoids unsupported high-rate 4K DV negotiation; it does not add high-rate DeckLink DV decoding.

Includes the previous RC6's Magewell capture changes, responsive background tone-chart preparation and Beta renderer libplacebo API 388 update. Standard renderer dependency remains 7.360.1; experimental gamut correction remains Off by default.

## Qualification

Fresh x64 Release build from merged beta **a8f62855c246019e3d9262f384e58c96d60f3147**, [PR #234](https://github.com/billslack2/videoprocessor/pull/234). All **37 updater policy checks** and **19 updater control checks** passed. Package hashes, payload inventory and portable configuration sample are verified. The EDID bytes match the previously verified BIN.

Testing was explicitly limited to this updater change. The unchanged native application previously passed 3,538 native tests and 326 installer lifecycle checks at 605ec4af; those suites were not rerun for this refresh. No new clean-Windows, physical capture/display or installed interactive Config qualification is claimed.

Windows 10/11 x64. Setup and ZIP include VP, both renderers, Config, updater and app-local runtimes. Setup preserves existing settings. For a fresh portable folder, copy VideoProcessor.cfg.example to VideoProcessor.cfg; preserve your existing file on upgrades. Previous RC6 releases remain available for rollback.

VP binaries and Setup are unsigned; updater metadata is signed. This release workflow does not deploy or restart the user's live installation.