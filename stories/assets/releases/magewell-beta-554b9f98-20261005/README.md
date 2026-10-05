# Magewell on current beta — 554b9f9871b2

Source: 554b9f9871b25b5992ddd280ba8d9e47d66d1fdc, current beta v1.3.005-beta at build time. Branch codex/magewell-optional was rebased/advanced and pushed to the same commit. Beta already contained Magewell, RGB12 compatibility, profiling tests, anamorphic changes and portable configuration correction.

- Setup: VideoProcessorSetup-1.3.005-beta-554b9f9871b2.exe
- ZIP: VideoProcessor-1.3.005-beta-554b9f9871b2-x64-Portable.zip
- SHA-256 sidecars accompany both artifacts.
- Fresh portable users copy VideoProcessor.cfg.example to VideoProcessor.cfg. Upgrades preserve existing settings. ZIP contains no active config.

Validation: complete x64 Release rebuild; 1,950 native tests; 49 installer preservation/recovery, 12 identity, 23 runtime and 8 portable configuration contract checks. Final ZIP sample matched source and manifest. Real installer lifecycle: 225 checks passed under separate QA registry and shortcut identity, with unchanged application payload. This covers fresh installation, same-build reinstall/repair, missing and damaged metadata, running-Config protection, uninstall, retained user files and manually deleted directory repair. Cross-version rollback and legacy ZIP adoption were not rerun. The QA registration was cleaned up.

Deployed 63 package files (including inventory) to C:\Videoprocessor\vp. Every replaced file was backed up under C:\Videoprocessor\vp\backups\magewell-beta-554b9f98-20261005-173315. Host and renderer hashes were verified together. Active configuration and state hashes are unchanged; no configuration edits. Application was not launched.

Unsigned distribution. Clean Windows without Visual Studio/runtime, interactive Config Apply/OK/reopen, and physical Magewell capture were not newly qualified. Missing-runtime execution was not rerun; static import isolation passed. No GitHub release assets or tags were changed.

See validation/release-receipt.json, validation/deployment.json and validation/installer-lifecycle.log for exact evidence.

This final build supersedes the interim 61fb90ad build after beta advanced during packaging. It includes PR #131 pipeline stutter telemetry and conversion-worker changes inherited from beta. A targeted static lifecycle/locking review found no obvious blocker; full tests and installer lifecycle passed on this exact build. The pre-task deployment backup also remains at C:\Videoprocessor\vp\backups\magewell-beta-61fb90ad-20261005-172645. Remote beta and Magewell branch both verified at 554b9f9871b25b5992ddd280ba8d9e47d66d1fdc after final deployment.


[Setup](E:/codex/releases/magewell-beta-554b9f98-20261005/VideoProcessorSetup-1.3.005-beta-554b9f9871b2.exe) · [ZIP](E:/codex/releases/magewell-beta-554b9f98-20261005/VideoProcessor-1.3.005-beta-554b9f9871b2-x64-Portable.zip) · [Receipt](release-receipt.json) · [Deployment](deployment.json).
