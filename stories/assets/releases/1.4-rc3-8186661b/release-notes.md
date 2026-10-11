# VideoProcessor 1.4 RC3 — corrected build

This replaces the original RC3 packages with merged build `8186661b675b4c44b0f15882a4d90d4a3cbae03a` (PR #142). It includes both the advanced tone-mapping Config UI and the matching VP renderer/configuration support. The original RC3 did not support the newer settings such as `black_cutoff`.

## Advanced DTM controls
Open **VP Renderer → Rendering → Tone mapping**.
- Target brightness, HDR target black level, tone-mapping algorithm, gamut mapping, peak detection, and contrast recovery.
- Advanced curve controls: knee adaptation and bounds, fallback knee, spline slope/contrast, BT.2390 knee offset, and Reinhard contrast.
- Peak analysis controls: percentile, smoothing period, scene-change thresholds, and black cutoff.
- Responsive sliders, validated ranges, and Auto/default handling. Available in local and remote Config.

The matching renderer reads and applies these settings. Existing configuration is preserved.

## Optional automatic updates
Open **Updates** below **DirectShow** in Config.
- Manual checks, tray notifications, or automatic installation; select **Beta** for RC releases.
- Full VP + Config and Config-only installations, using Setup or portable packages.
- Signed update metadata and verified package hashes.
- Automatic installation waits for VP to close unless playback interruption is enabled. Running VP restarts afterward; Config returns to its prior visible or tray-only state.
- Updates affect the local installation; selecting a remote VP does not update that computer.

This replacement has a newer update sequence, so the original RC3 can update to it despite retaining the RC3 version name.

## Downloads
- **VideoProcessorSetup…exe**: full VP and Config.
- **VideoProcessorConfigSetup…exe**: Config only.
- **Portable ZIPs**: corresponding full or Config-only packages.

For a fresh full portable installation, copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`. Upgrades preserve your existing configuration.

GitHub pre-release. Update metadata is signed; Windows executables do not have a publicly trusted publisher signature. Only installer EXEs, portable ZIPs, and the required `vp-update.json` are published. Validation records are retained internally.

## Validation
All 2,373 core tests passed. Advanced tone-mapping Config round-trip, native Windows update shutdown, local update scope, 29 updater policy checks, 19 updater control checks, isolated full/Config-only portable update lifecycle checks, and packaging/runtime/configuration contract checks passed. The native-message shutdown test was rerun successfully with the Windows backend after an offscreen-backend failure. This build was not requalified on a clean Windows VM.
