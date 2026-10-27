# Earlier AR improvements integrated with the latest local subtitle build

The user explicitly requested rebasing the previously tested AR improvements onto the latest local build before deployment. Installed host, stable/beta renderers, Config and Config discovery hashes matched local subtitle commit `f22e22c4f0dcd9be87c26189fc18a83a798c9a26`. Remote beta was independently discovered/fetched at `7394ba420999a8fa708718ee4ce4d87e7459369f` on `v1.4.02-beta`; the user's local-base override was used.

The clean integration branch `codex/vp-0218-local-integration-20261027` is pushed at `f4219a7aa679c57f1f6d637cb08406b9c2b4b97b`. The AR production patch `e211bdb9` rebases exactly to `daad7eb0`, and test-fixture repairs `ddf5d666` rebase exactly to `f4219a7a`. [Range-diff](range-diff.txt) records exact patch equivalence. No new variable-profile/textured-ad route is enabled. No PR was merged and no public package was published.

## Validation

- Clean full x64 Release solution build: exit 0, pinned exact source before/after.
- Fresh combined native suite, no filter: **3439/3439 passed**, no failures or unexecuted results. [Qualification](qualification.json) reconciles the TRX counters, unique method inventory and exact test DLL hash. All 3433 prior effective methods are covered.
- Config test `subtitle background shape round trip and visibility`: passed with the fresh test executable. The first PowerShell wrapper treated benign stderr as an exception; the corrected process wrapper uses the actual exit code. Original harness is retained; no test/source changes were needed.
- [Two Luna reviews](luna-review.txt): exact patch preservation, no source-file overlap with subtitle changes, route/subtitle ownership semantics, and deployment safeguards reviewed with no remaining blocker.

## Deployment

[Receipt](deployment-receipt.json) records deployment at `2026-10-27T12:43:00.6679053-04:00`, 14 payload files, exact hashes, before/after/backup values and 24 retained dependency hashes. Host, stable/beta renderers, Config and discovery come from the same successful Release build; runtime records match. All five installed binaries were checked against the selected local base immediately before replacement. Payloads were staged and hash-verified on the target volume, then installed with atomic per-file replacements.

The first deployment attempt encountered PowerShell 5 null-to-empty conversion of the optional `File.Replace` backup filename. Replacement failed on the first file; caught rollback succeeded, and all 14 payloads plus both settings files were independently confirmed unchanged. The original script, plan and failure record are preserved. The corrected script uses an explicit same-volume backup path and verifies its displaced-original hash; a disposable-file probe and Luna review passed before the successful retry. No product source or test was changed.

Backup: `C:\Videoprocessor\vp\backups\ar-local-f4219a7a-20261027-124259`. Active `VideoProcessor.cfg` and `VideoProcessor.state` were backed up and remained byte-identical. Configuration edits: **none**. Both applications had already closed normally before deployment; neither was forced closed or restarted. Runtime initialization and physical playback acceptance remain pending for the next normal launch.

## Limits

The earlier contained raised-matte and independent/native proof improvements are restored alongside the latest subtitle guard/internal padding changes. The captured textured YTTV ad remains unresolved; research proposals were not included. Optional capture methods without their environment inputs do not establish recorded-input replay. Functional suite timings are not isolated performance measurements. VP-0218 remains In Progress for the textured-ad route, moving-scene/physical acceptance and separate dark-owner/full-raster work.
