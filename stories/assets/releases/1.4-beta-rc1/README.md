# VideoProcessor 1.4 RC1 release evidence

Published [1.4 RC1](https://github.com/billslack2/videoprocessor/releases/tag/1.4-beta-rc1) as an unsigned GitHub prerelease on 2026-10-06 (local time). The `1.4-beta-rc1` tag points to clean `v1.4.00-beta` commit `b801730610a095826799275084ce65a29a0fc167`. Package version: `1.4.00-rc1`; release branch: `codex/release-1.4-rc1` at the same commit.

- Setup: [VideoProcessorSetup-1.4.00-rc1-b801730610a0.exe](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc1/VideoProcessorSetup-1.4.00-rc1-b801730610a0.exe), SHA-256 `157530AA051454552E3EC4BCDF525DBA5B39E6662E29ED66190208A093E56AE0`.
- Portable: [VideoProcessor-1.4.00-rc1-b801730610a0-x64-Portable.zip](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc1/VideoProcessor-1.4.00-rc1-b801730610a0-x64-Portable.zip), SHA-256 `3134B151146769B94FFF4B015C109F9FF6BDD87BE06F7A4BF15B4DBDD546E411`.
- Both individual `.sha256` sidecars were published. All four GitHub asset byte sizes and SHA-256 digests matched the local exports.

The full x64 Release solution rebuild passed. Native tests initially reported one failure in `LegacyEqualsAssignmentsRemainReadable`; that test passed in isolation. A full suite rerun with a private temp directory passed 1,965/1,965. The build receipt and four binary hashes were reverified before packaging. Installer support (49), identity (12), runtime packaging (23), portable contract (8), and finished ZIP sample checks passed. The built host and Setup report `Bill Slack` in version metadata; both are unsigned.

A real installer lifecycle run under the isolated `CodexSandboxOffline` identity stopped at the first install: setup detected existing VideoProcessor Config processes on the host and correctly refused to continue. Those processes and the user's installation were left untouched. Clean Windows without Visual Studio, full setup lifecycle, interactive Config Apply/OK/reopen, and physical hardware qualification remain unverified for this RC.

[Release notes](release-notes.md) · [Build receipt](release-receipt.json) · [Checksums](SHA256SUMS.txt).
