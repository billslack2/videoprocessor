# VideoProcessor 1.4 RC4

## Changes since RC3
- Optional **VP Renderer Beta**, based on a newer libplacebo build, with separate experimental highlight mapping and gamut overflow correction controls. The existing VP Renderer remains available.
- Subtitle script preferences for Latin, Arabic, CJK and Thai, with caption grouping and continuity improvements.
- Shared rule expressions support `~=` for literal substring matching.

## Downloads and remote Config
RC4 ships one full **Setup EXE** and one full **x64 Portable ZIP**. Both include VideoProcessor, its renderers, Config and the updater. There is no separate Config-only ZIP or installer for RC4.

For remote configuration, we recommend launching `config\VideoProcessorConfig.exe` from a file share of the VP installation, keeping the complete installed directory structure available. Use Config's remote connection to select the VP computer. This lets remote users launch the Config version supplied with that VP installation rather than maintaining separate copies. Close shared Config sessions before updating the host, then reopen them afterward. Perform installation and updates on the VP host itself; launching Config over a share is not a remote deployment mechanism.

Existing Config-only installations do not automatically migrate to the full package. Switch remote-use shortcuts to the shared Config executable, or install the full package in a suitable separate local folder.

For a fresh portable folder, copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`. Preserve your existing settings when upgrading. Older releases may not understand newer settings if you roll back.

Optional automatic updates remain available for full installations; select the Beta channel for release candidates. Signed `vp-update.json` metadata accompanies these packages. Windows application and installer binaries remain unsigned.

## Source and validation
Application source matches beta `a1853906806a459e33439718690e0f1ce7741b59`. Release commit `6d5f8ad4241c24b750b96d2e3f54abb458bc7986` adds packaging fixes for the additional renderer and its license files, plus test expectations updated for explicit Arabic mode and renderer API 25. Application behavior is unchanged from that beta tip.

- Clean x64 Release rebuild; all **2,382 native tests passed**.
- Installer support, identity, runtime dependency/hash, and portable sample-configuration checks passed.
- **225 actual-installer lifecycle checks** passed under an isolated QA registration/shortcut identity with matching managed application payload hashes: fresh install, running-Config refusal, repair, data preservation, uninstall/reinstall, and deleted-folder recovery.
- 29 updater policy checks, 19 updater control checks, isolated portable update transactions, and native Config shutdown/update-scope checks passed.

The initial native run exposed seven outdated API/script-mode expectations; the corrected focused suite passed all 130 tests before the final clean rebuild and complete passing run. Validation evidence and checksums are retained in the story tracker rather than published as download assets.

Windows 10/11 x64. Clean Windows without Visual Studio, interactive Apply/OK/reopen persistence, cross-version rollback, second-computer file-share operation, and physical capture/playback were not newly qualified for this RC. The installer lifecycle checks used this development machine, not a clean VM. Windows binaries remain unsigned; only update metadata is signed.
