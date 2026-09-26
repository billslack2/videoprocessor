# Local ZDF crop-acquisition test package — September 26, 2026

Requested local deployment and setup/portable packaging, not a beta merge or GitHub release publication.

Source: ff53b4683a2a6b59423f05918e29ce1e0c8cdca8 plus reviewed local changes, source fingerprint db4e57812fa609a755e69828fae1e347bca0ad922509b9953916e548bc0bcbb1. Full source snapshot and exact delta are preserved with the build evidence. This correction blocks new crop admission through pending retention owners and current contradictory visible-pixel evidence; established crop handling and detection thresholds are unchanged.

- Complete x64 Release rebuild and all 1,714 tests passed.
- 49 installer preservation/recovery, 12 identity, and 23 runtime checks passed.
- Every setup payload file and portable ZIP manifest entry was hash verified; SHA-256 sidecars match exact distribution bytes.
- Matching Release host/renderer deployed to C:\Videoprocessor\vp and verified loaded in process 51536. Active configuration unchanged.
- Backup: C:\Videoprocessor\vp\deployment-backups\zdf-crop-acquisition-20260926-092630.
- Unsigned test build. Real installer lifecycle and clean-Windows/interactive Config qualification were not repeated. No incoming frames at post-deployment check; visual validation remains pending.

[Setup](E:/codex/videoprocessor/packages/zdf-crop-acquisition-20260926-db4e57812fa6/VideoProcessorSetup-1.3.005-beta-ff53b4683a2a-dirty-db4e57812fa6.exe) · [Setup SHA-256](E:/codex/videoprocessor/packages/zdf-crop-acquisition-20260926-db4e57812fa6/VideoProcessorSetup-1.3.005-beta-ff53b4683a2a-dirty-db4e57812fa6.exe.sha256)

[Portable ZIP](E:/codex/videoprocessor/packages/zdf-crop-acquisition-20260926-db4e57812fa6/VideoProcessor-1.3.005-beta-ff53b4683a2a-dirty-db4e57812fa6-x64-Portable.zip) · [ZIP SHA-256](E:/codex/videoprocessor/packages/zdf-crop-acquisition-20260926-db4e57812fa6/VideoProcessor-1.3.005-beta-ff53b4683a2a-dirty-db4e57812fa6-x64-Portable.zip.sha256)

[Package receipt](package-receipt.json) · [Investigation](E:/codex/videoprocessor/zdf-crop-acquisition-20260926/artifacts/zdf-regression/REVIEW.md) · [Deployment receipt](E:/codex/videoprocessor/packages/zdf-crop-acquisition-20260926-db4e57812fa6/deployment.json)

Story acceptance/status is unchanged by this test-package evidence.
