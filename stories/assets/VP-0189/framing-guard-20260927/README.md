# Framing guard test package — 2026-09-27

Source: https://github.com/billslack2/videoprocessor/commit/2f0e9f3057736a36cf7ca651bcbdc4162d7d7449

Test branch codex/framing-guard-package-20260927, based on current beta ab683eefe5fdffa018b788eedfcd8913dbebe1b4. Not merged. Historical VP-0189 status is unchanged; these follow-up changes await field validation.

Output directory: E:\codex\videoprocessor\packages\framing-guard-20260927-2f0e9f305773

- Setup: VideoProcessorSetup-1.3.005-beta-2f0e9f305773.exe
- ZIP: VideoProcessor-1.3.005-beta-2f0e9f305773-x64-Portable.zip
- Matching .sha256 sidecars are alongside both files. Exact hashes are in release-receipt.json.
- x64 Release rebuild and 1,813/1,813 tests passed. Fill-reference regression tests demonstrated three failures before the fix. Independent reviews found no blocker.
- Host/renderer deployed together and verified in C:\Videoprocessor\vp. Config unchanged. Backup listed in deployment.json.
- Unsigned. Installer lifecycle safely refused because VP/Config were open; clean-Windows and interactive Config qualification not performed. See PACKAGE-NOTES.txt for remaining limits and suggested playback tests.
- Correction prevents logged optional-fill zoom amplification; the original false bar acquisition remains unproven without source-image evidence.
