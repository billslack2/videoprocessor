# VideoProcessor 1.4 RC3

## Highlights

### DTM configuration in the Config app
Open **VP Renderer → Rendering → Tone mapping** (expand the section) to adjust:
- Target brightness in nits and HDR tone-map target black level.
- Tone-mapping algorithm: Auto, spline, BT.2390, ST 2094-40, or Reinhard.
- Gamut mapping, dynamic peak detection, and contrast recovery.

These settings are available through the local and remote Config UI. They apply to VP Renderer; other renderers retain their own controls.

### Optional GitHub-hosted automatic updates
Open **Updates**, directly below **DirectShow** in the Config sidebar.
- Choose manual checks, tray notifications, or automatic installation.
- Select **Beta** for release candidates such as RC3. Channels are identified by signed update metadata.
- Supports full VP + Config and Config-only installations, installed using Setup or a portable package.
- Updates verify signed metadata and package hashes before installation and preserve existing configuration.
- Automatic installation waits for VP to close by default. The optional playback setting allows it to stop and restart VP.
- If VP was running, it restarts after a successful update. Config returns to its previous state: visible window or tray-only.
- Updates apply only to the local installation. Selecting a remote VP does not update that computer or close its Config instances.

## Downloads
- **VideoProcessorSetup…exe**: full VP and Config installation.
- **VideoProcessorConfigSetup…exe**: Config only, for a second computer.
- **Portable ZIPs**: corresponding full or Config-only portable files.

Older installations without the updater need one manual upgrade to RC3. For a fresh full portable installation, copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`; keep your existing configuration when upgrading.

This is a release candidate, published as a GitHub pre-release. Source: `582f82dfa486e55c211aba8f745300c458c79aff` (PR #141). Update metadata is signed; the Windows executables do not have a publicly trusted publisher signature.

## Validation
All 2,371 core tests passed, plus release packaging checks and focused Config updater/shutdown checks. This release was not requalified on a clean Windows VM. Build and validation records are retained internally.

