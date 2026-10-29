# RC6 DV and profile update — October 29, 2026

Updated the existing [RC6 release](https://github.com/billslack2/videoprocessor/releases/tag/1.4-beta-rc6). The RC6 tag and previous downloads are retained; the new commit-bearing filenames and signed updater descriptor identify this update. Public assets contain only Setup/portable packages and the signed vp-update.json.

- [Updated Setup](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc6/VideoProcessorSetup-1.4.0-rc6-d5607e080537.exe)
- [Updated portable ZIP](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc6/VideoProcessor-1.4.0-rc6-d5607e080537-x64-Portable.zip)
- [Profile fix PR #224](https://github.com/billslack2/videoprocessor/pull/224)
- [DV fix PR #225](https://github.com/billslack2/videoprocessor/pull/225)

Production application source matches beta c716bd8108b03b0ef279c14c584e097eecc29515. Clean, pushed release source d5607e080537ad2713e8e746b78226dda44162cb on codex/rc6-dv-profile-release-20261029 carries forward the published RC6 packaging and test-only subtitle corrections. Release sequence: 20261029034608. Built with x64 Release, the installed v142/v143 toolsets, Qt 6.8.3 and Inno Setup 6.7.3.

All 3,504 native tests passed (including the standard GPU/cue classes), plus 86 beta-DV/capture tests with real renderer integration, 407 targeted native regressions (a subset of the full suite), nine targeted Config profile-status UI regressions, 225 actual-installer lifecycle checks under a unique QA identity, 59 installer support, 12 identity, 23 runtime, 29 updater policy and 19 updater-control checks. Portable configuration and payload hashes passed. GitHub asset digests and a fresh public updater download match the local signed exports. The initial targeted run exposed stale ABI-30 assertions; commit 17c26a65 corrected both to the required ABI 31 before the passing runs.

DV fixes remain untested on real capture hardware. No Magewell card is installed yet. Synthetic/SDK-mock/GPU integration tests do not qualify HDMI negotiation, capture-card byte preservation or hardware range restoration. Clean Windows without Visual Studio, actual installed Config Apply/OK/reopen, cross-version rollback and physical display acceptance are not newly qualified. Windows binaries/Setup are unsigned; updater metadata is RSA/SHA-256 signed with the existing pinned release key. This release update did not deploy to the live installation or change its configuration, registration or story statuses.

Receipts, installer manifest and hashes here are internal release evidence. Full logs/TRX and isolated installer evidence remain under E:\codex\diagnostics\release-rc6-dv-profile-20261029 and the source worktree artifacts directory. The preparatory workflow interrupted for the RC6-label correction is preserved separately and is not passing evidence.
