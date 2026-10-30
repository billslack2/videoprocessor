# Stable RC6 - DeckLink EDID update (2026-10-30)

Published [Stable RC6](https://github.com/billslack2/videoprocessor/releases/tag/1.4-rc6-decklink-20261030) from clean merged beta commit 605ec4af39b934ac4bbd0f3e7dbbe7e31bec7e63, after PRs [232](https://github.com/billslack2/videoprocessor/pull/232) and [233](https://github.com/billslack2/videoprocessor/pull/233). The release branch is codex/rc6-decklink-release-20261030. The tag and all three GitHub asset digests were verified; the public signed updater descriptor was downloaded and matched its verified local bytes. Stable sequence: 20261030162702.

- Fresh x64 Release build; all 3538 native tests passed.
- Six Config tone-chart regressions against the packaged renderer passed.
- Installer support, identity, runtime, portable configuration and updater-policy checks passed; 19 updater-control checks and two portable-update fixtures passed.
- 326 isolated real-installer lifecycle checks passed, including upgrade/rollback against previous RC6 5b8132b6, repair, running-Config refusal, uninstall and data preservation. Corrupt-package recovery restored previous bytes and preserved settings. QA installer/helper/manifest identities were changed consistently; application payload hashes remained those of their respective release builds.
- VP binaries and Setup are unsigned; updater metadata is signed and verified. No live deployment or configuration changes were made by this release workflow. VP-0189 remains in Review.

The optional [Vertex2 EDID guide](https://github.com/billslack2/videoprocessor/blob/605ec4af39b934ac4bbd0f3e7dbbe7e31bec7e63/docs/edid/README.md) records low-rate UHD25 DV and UHD59.94 HDR10 playback observations. These hardware observations preceded the fresh release build. Post-change true UHD50 fallback and exact60 live playback remain unqualified. Installing VP does not apply an EDID.

Clean Windows without Visual Studio and installed interactive Config Apply/OK/reopen were not newly qualified. Initial incomplete/failed QA runs are retained locally: a superseded native run was stopped to merge the existing RC6 test-only corrections; two QA identity fixtures were corrected; the offscreen Config fixture needed the matching Qt test platform plugin. No failure was bypassed in the published qualification.

[Downloads and release notes](release-notes.md), [qualification](release-verification.json), [build receipt](build-release-receipt.json), [checksums](SHA256SUMS.txt), [native test summary](native-test-summary.txt) and [additional test logs/workflows](additional).

Local export: E:\codex\releases\VideoProcessor-1.4.0-rc6-decklink-605ec4af-20261030. Source and complete logs: E:\codex\videoprocessor\rc6-decklink-release-20261030. The existing live VP installation and the tracker's unrelated VP-0207 design edit were preserved.