# auto-update-test

Published test release for exercising VideoProcessor automatic updates.

- No-op source commit: `c2fe3427a18cf211d75ad106efe651c195fe1be5`.
- Identical source files to the isolated updater test build `505f95e829db0cc485d4a50e26078982a7d1bcb3`; version/build identity and signed update sequence advance.
- Includes full VP and Config-only x64 installers. Both also update compatible portable copies in place through the updater.
- Update channel: beta. This test release is not marked GitHub Latest.
- RSA/SHA-256 signed updater metadata; Windows installer publisher signing is not enabled.
- For the isolated test copy, open VideoProcessorUpdate.exe and check the Beta channel. Existing installations without the updater cannot discover this release.

Source is on the `codex/github-updater-20261010` feature branch based on beta `19d744402ef3acd518053f673f7dffdfd7a16163`; this release does not merge the feature into beta.

Qualification: x64 Release build, full native test suite, updater policy tests, installer preservation/identity/runtime checks, and isolated full/Config-only portable installer lifecycle checks. Clean Windows without Visual Studio and live end-to-end app shutdown/restart remain separate user testing.

Qualification history: the first full run had one configuration-cache failure. The unchanged test and its cache group passed in isolation; the original failure and follow-up evidence are retained alongside the final full-suite result. No tests were excluded and no application code was changed for this release.
