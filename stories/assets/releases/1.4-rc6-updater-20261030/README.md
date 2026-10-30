# RC6 updater refresh - 2026-10-30

At the user's explicit request, replaced the packages on [the existing RC6 release](https://github.com/billslack2/videoprocessor/releases/tag/1.4-rc6-decklink-20261030) after merging [PR #234](https://github.com/billslack2/videoprocessor/pull/234). The same tag was moved with an exact old-commit lease from 605ec4af39b934ac4bbd0f3e7dbbe7e31bec7e63 to a8f62855c246019e3d9262f384e58c96d60f3147. Prior metadata and local packages are retained; other RC6 releases were not changed.

Beta now accepts Stable and Beta releases, choosing the highest eligible signed sequence. Stable accepts only Stable. Existing replay, installed-sequence, package-flavor and signature protections remain. Older Beta-only updaters need Stable selected once or a manual install to acquire this fix.

Fresh x64 Release rebuild. All 37 updater policy tests and 19 helper-control checks pass. The packaged updater finds the replacement Stable RC6 on both selected channels using the real public signed feed and in-memory preferences only. A short publication cache interval initially returned the previous valid descriptor; the unmodified discovery path subsequently returned the new commit on both channels. No local updater settings, VP installation or playback were changed.

Per the user's explicit instruction, only the updater change received functional testing. The unchanged native suite was not rerun; the preceding 605ec4af release passed 3,538 native tests and 326 installer lifecycle checks. Package inventory, artifact hashes and portable sample configuration were verified. VP binaries and Setup remain unsigned; updater metadata is signed. No new clean-Windows, installed interactive Config or hardware qualification is claimed.

Every release must now include the separate vertex2-lg-dv30-hdr10bit.bin download. The checked-in builder stages it with an internal checksum; repository release instructions and the personal vp-release skill/exporter preserve this requirement. The published BIN hash remains 9a271cf7e071bf247a1804efda3a62fdc182e161951c1097c40484c210956b01 and both EDID blocks have valid checksums. Applying it remains optional and manual.

[Release notes and downloads](release-notes.md), [focused build receipt](release-receipt.json), [public asset verification](github-public-verification.json), [real feed discovery](discovery-after-publication.json), [checksums](SHA256SUMS.txt).

Local artifacts: E:\codex\releases\VideoProcessor-1.4.0-rc6-updater-a8f62855-20261030. Complete build log: E:\codex\videoprocessor\updater-beta-includes-stable-20261030\artifacts\rc6-updater-refresh. Release sequence: 20261030173927. VP-0189 remains in Review; no story status change.
