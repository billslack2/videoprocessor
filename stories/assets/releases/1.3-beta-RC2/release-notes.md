VideoProcessor **1.3 RC2** for Windows 10/11 x64, built from `38477f13560a3c4f0085c8840127ae064d0a5b86` on the current `v1.3.005-beta` integration branch. Package version: `1.3.005-rc2`.

## Downloads

- **Installer:** `VideoProcessorSetup-1.3.005-rc2-38477f13560a.exe`
- **Portable ZIP:** `VideoProcessor-1.3.005-rc2-38477f13560a-x64-Portable.zip`
- SHA-256 sidecars and `SHA256SUMS.txt` verify the downloaded files. `release-verification.json` records source identity and qualification results.

Both packages contain the same application build and bundled Microsoft runtime DLLs. No separate system-wide Visual C++ runtime installation is required. Runtime packaging verifies the x64 CRT floor of 14.44.35211.0 and the MFC toolset floor of 14.29.30133.

## Changes since RC1

- A per-user Windows installer with custom-folder installation, upgrade, repair, rollback, and preservation of configuration, state, shaders, LUTs, logs and other user data. No desktop shortcut or administrator installation is required.
- Configuration audit fixes across startup defaults, inherited values, profile identity, validation, diagnostic labels and documentation. Invalid settings remain visible for correction instead of being silently dropped.
- DirectShow **Auto frame offset is consistently 90 ms**. The built-in renderer keeps its neutral timestamp offset.
- HDR target black defaults to **0**, mapped internally to **0.000001 nit**. Legacy `AUTO` now means the same zero default; existing measured numeric values remain supported. A profile named Base works when unambiguous; root-plus-Base collisions report an actionable error.
- Crop and aspect-transition fixes for dark scenes, gradual picture expansion and small detector changes; screen-profile actions are preserved while rendering scripts finish.
- Display-calibration LUT and SDR transfer updates, LLDV/profile improvements, P010/chroma conversion options, and HDMI/EOTF transition and renderer recovery fixes.

The separate experimental subtitle bounding-box trial is not included in RC2.

## Install or update

Close VP and exit Config from its tray menu before running Setup. Run Setup normally, without elevation. For an existing ZIP installation, select its actual application folder; existing operator configuration and assets are preserved. Subsequent installs reuse the registered location.

For the portable ZIP, extract into a writable folder. On a fresh installation, copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`. Preserve your own configuration and custom assets when updating an existing installation.

Older installers/builds may not understand newer settings. Back up your configuration before testing a rollback.

## Validation and limitations

- Clean x64 Release build and all **1,386 native tests** passed.
- **49** installer preservation/recovery checks, **12** source-identity checks, and **23** runtime-packaging checks passed.
- The same application source tree previously passed all **80 configuration-editor tests**.
- **326 actual-installer lifecycle checks**, two legacy ZIP-adoption cases, and deliberate corrupt-package recovery passed under an isolated QA identity. Application payload hashes matched the public build; QA registry/shortcut identities and process scoping kept the active installation separate. Details are in the attached verification file.

These packages are **unsigned**; Windows may show an unknown-publisher or SmartScreen prompt. Checksums verify file integrity, not publisher identity. Clean Windows without Visual Studio, absent/old global runtime scenarios, and physical HDMI/capture behavior were not newly qualified for this RC. Lifecycle results on the development host do not establish that clean-machine coverage.

[Changes since the RC1 tag](https://github.com/billslack2/videoprocessor/compare/1.3-beta-RC1...1.3-beta-RC2)
