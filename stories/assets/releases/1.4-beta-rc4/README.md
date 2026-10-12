# VideoProcessor 1.4 RC4 — October 11, 2026

Published [GitHub prerelease](https://github.com/billslack2/videoprocessor/releases/tag/1.4-beta-rc4) at the user's request.

- [Full Setup](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc4/VideoProcessorSetup-1.4.0-rc4-6d5f8ad4241c.exe)
- [Full portable ZIP](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc4/VideoProcessor-1.4.0-rc4-6d5f8ad4241c-x64-Portable.zip)

No separate Config-only installer or ZIP is published. Config is included in the full package; notes recommend launching it from a file share of the VP installation and updating on the host after closing shared Config sessions. Existing Config-only installs skip this full-only release rather than migrating automatically. The sole public JSON is signed vp-update.json; sequence 20261012012000 exceeds RC3's verified 20261011141457.

## Provenance

Latest GitHub beta/default branch v1.4.01-beta was fetched at a1853906806a459e33439718690e0f1ce7741b59 and rechecked unchanged before publication. Clean isolated release checkout: E:\codex\videoprocessor\release-1.4-rc4-latest. Release branch codex/release-1.4-rc4-latest and release tag both identify 6d5f8ad4241c24b750b96d2e3f54abb458bc7986. This release branch adds the five-binary build-receipt correction, preservation of Beta renderer license text, public-asset guidance, and tests aligned with API 25 and explicit Arabic mode. Application sources outside VideoProcessor-Test are identical to the beta tip. No unrelated branch was merged.

## Verification

Fresh x64 Release rebuild; 2,382/2,382 native tests passed. The initial current-beta run passed 2,375 with seven stale API/script-mode assumptions; its failures and the exact test-only corrections are preserved here. The focused 130-test run passed before the final clean rebuild. Packaging passed 59 installer support, 12 identity, 23 runtime checks, eight portable contract fixtures, and finished-archive configuration validation. All three published asset sizes/digests match local exports.

225 real full-installer lifecycle checks passed with a unique QA registration/shortcut identity and unchanged managed payload hashes. Coverage includes fresh installation, running-Config refusal, app-local runtime loading, repair, preservation, uninstall/reinstall, deleted-folder recovery and registration cleanup. 29 updater policy and 19 control checks, full/config isolated portable transactions, and native Config shutdown/local-scope tests passed. Updater/Config source is unchanged by the release-only test corrections.

The update descriptor signature was independently verified against the pinned public key. Windows binaries are unsigned. Clean Windows without Visual Studio, interactive Apply/OK/reopen persistence, cross-version rollback, second-computer file-share operation, and physical capture/playback were not newly qualified. Lifecycle QA ran on the development machine, not a clean VM.

No deployment or active-configuration changes were made. VP-0189 status is unchanged. Exact tool inputs, hashes and receipts are attached; local full evidence and exports remain at E:\codex\releases\VideoProcessor-1.4.0-rc4-6d5f8ad4 and the release checkout's artifacts directory. Byte-identical rebuilds are not claimed.
