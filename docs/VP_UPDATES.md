# GitHub-hosted updates

## Scope

The Windows player and Qt Config app launch a shared, separate
`VideoProcessorUpdate.exe`. It uses GitHub Releases in
`billslack2/videoprocessor`; no service, account token, or private update server
is required. Public releases are required for this unauthenticated client.

Both Setup and portable ZIP packages include the updater. Older packages without
it need one manual replacement/upgrade first. Development/dirty builds cannot
update automatically. Portable updates use the same signed setup in a dedicated
in-place mode that creates no registration, shortcuts, or uninstaller. A separate
registered copy is left untouched. Existing ownership metadata is required.

Two products have independent per-user Inno identities:

| Product | Updated together | Default folder |
| --- | --- | --- |
| Full | Player, renderer, Config, updater and dependencies | `%LOCALAPPDATA%\Programs\VideoProcessor` |
| Config-only | Config, updater and Config dependencies | `%LOCALAPPDATA%\Programs\VideoProcessorConfig` |

Config-only setup cannot overwrite full-install ownership or a portable player
folder. Full setup cannot overwrite a Config-only inventory. Setup-installed updates
must retain their existing installation root, including silent setup. Another
running installation does not normally block an update; inability to inspect a
VP process fails conservatively.

## User flow and playback

- VP exposes **Check for updates** in its window system menu. Config exposes
  **Check for updates on this computer** in Help and the tray menu.
- Apps launch a background helper after 30 seconds and every six hours while
  running. Shared per-installation preferences limit network checks to once per
  day. Stable/beta selection, automatic-check preference, skipped release, and
  the highest trusted sequence seen are persisted per Windows user and root.
- Update mode is optional and persistent: **Manual checks only**, **Notify in
  tray** (default), or **Install automatically without prompts**. Manual checks
  bypass the daily throttle. Automatic installation must be explicitly enabled.
- Automatic mode waits until the local VP process closes, checking once a minute
  while an update is queued. A separate, off-by-default checkbox permits stopping
  playback and restarting VP. Config may remain running in the tray; it exits
  without saving and restarts after the update. No Windows service is installed;
  one app must have launched the helper to discover/queue an update.
- Unattended installs use silent setup with dialogs suppressed. Errors appear in
  the tray and updater window; failed updates are not repeatedly retried. Changes
  to remote protocol/settings compatibility require manual review. Skipped
  releases are not installed automatically. Turning off automatic mode cancels
  queued installation; already-started setup runs to completion.
- Download and signature/hash verification happen off the applications' UI
  threads, in the helper. No decoding, rendering, or configuration RPC polling
  is added to playback.
- Manual **Install and restart** explicitly confirms playback interruption. Config
  exits before VP, including its tray process, without prompting or saving
  unsaved edits. The install confirmation describes this behavior.
  Apps are asked to exit; they are never forcibly terminated.
- A root-scoped file lock prevents these new apps from starting during install.
  The helper runs from a user-local cache so setup can replace its installed EXE.
- Existing Inno transactional install/recovery is reused. The helper verifies the
  exact new inventory and every managed file before restarting the applications
  that were running, with their original command-line arguments. User settings,
  state, custom shaders and LUTs retain existing installer preservation rules.
- A failure does not automatically restart potentially mixed binaries. The error
  names the setup log and directs the user to recover by rerunning setup.

Updating Config on one computer never deploys to its remote VP target. Release
metadata includes RPC/settings compatibility. A changed compatibility version
warns that the other machines must be updated; normal connection-time RPC
compatibility checks remain authoritative.

## Trust model

The pinned RSA public key is `packaging/updates/UpdatePublicKey.xml`, embedded in
the helper at compilation. A release publishes `vp-update.json`, an envelope
containing base64 payload bytes and an RSA/SHA-256 PKCS#1 v1.5 signature over those
exact bytes. The client verifies before interpreting the payload.

The signed payload binds schema/updater version, channel, global release sequence,
x64 architecture, exact source commit, display version, expiry, compatibility,
notes, and package flavor/URL/size/SHA-256/installation-manifest SHA-256. Only this
repository's HTTPS release assets are accepted; redirects are restricted to
GitHub's download hosts. Size limits, bounded parsing and network timeouts apply.

Sequences must increase **globally across stable and beta**; use a UTC timestamp
such as `20261010153000`, not the display version or Git hash. Switching channels
does not permit downgrading. The client also refuses rollback below a higher
trusted sequence it has previously seen. Preferences are user-local, so resetting
or replacing that state is outside this rollback protection.

A self-signed RSA certificate was initialized in the build user's
`Cert:\CurrentUser\My` store. Its private key is non-exportable and is never
written into the source tree, setup, or release metadata. Inspect the certificate
named `CN=VideoProcessor Release Manifest Signing` to obtain its thumbprint.
`tools/new_update_signing_key.ps1` refuses to replace an existing pinned key.
Keep this Windows account and its key available for signing. Losing this key
requires a new manual trusted bootstrap; automated key rotation is not implemented.

This signs **update metadata**, not Windows publisher trust. Setup still has the
existing unsigned-publisher/SmartScreen behavior. A publicly trusted Authenticode
certificate may be added later without changing the metadata verification scheme.
Do not install the self-signed certificate into Windows trusted-root stores.

## Build and publish

Build prerequisites are the existing x64 Release toolchain, Qt, Magewell SDK,
Inno Setup 6.7.3 and official VC redistributables. The helper is C# targeting the
Windows .NET Framework 4.x runtime (included with supported Windows 10/11), with
no NuGet or Qt dependency. It is compiled by packaging, not the native solution.

1. Commit the tested changes on the integration/release source before packaging.
   Source identity and build receipts must match exactly. Never publish dirty
   builds. Set the installed SDK path if it is not globally configured.
2. Pick one increasing sequence, one channel and one exact display version for
   both packages. Run the existing qualified installer workflow with these new
   arguments, using actual paths for compiler/redist/runtime inputs:

   ```powershell
   tools/build_installer.ps1 -CoreVersion 1.4.0-beta -Flavor full `
     -ReleaseSequence 20261010153000 -UpdateChannel beta `
     -VcRedistPath <official-redist> -IsccPath <Inno-6.7.3-ISCC>
   tools/build_installer.ps1 -CoreVersion 1.4.0-beta -Flavor config -SkipBuild `
     -ReleaseSequence 20261010153000 -UpdateChannel beta `
     -VcRedistPath <official-redist> -IsccPath <Inno-6.7.3-ISCC>
   ```

   Add `-PortableZip` to either flavor to export an updater-enabled ZIP.
   Each EXE receives `.sha256` and `.manifest.json` sidecars. The latter contains
   the exact inventory bytes embedded in setup, not a reconstructed inventory.
3. Qualify both installers and upgrade/recovery behavior in disposable Windows
   installations. Do not use a live playback installation for lifecycle tests.
4. Write release notes. Generate the descriptor with the signing certificate:

   ```powershell
   tools/new_update_release.ps1 -InstallerPaths <full-exe>,<config-exe> `
     -ReleaseTag <tag> -CertificateThumbprint <thumbprint> -NotesPath <notes>
   ```

   This refuses mismatched source identities, package hashes, flavors or release
   metadata. The default descriptor lifetime is 90 days. Renew it before expiry
   by re-signing the same verified source/packages; never change payload bytes
   after signing. Expired metadata is rejected rather than silently trusted.
5. Upload the exact EXEs, inventory/hash sidecars and `vp-update.json` to a draft
   GitHub release whose tag matches the descriptor URLs. Publish after testing.
   These tools do not publish, push, merge, or deploy automatically.

Portable mode refuses folders with orphaned uninstall records or another product
inventory. The normal setup path remains available for deliberate adoption.

The first released updater cannot be tested against old releases lacking
`vp-update.json`; those are skipped. A complete public rollout needs a bootstrap
release followed by a newer signed release. No release has been published as part
of this implementation.

## Validation

- `tools/test_updater.ps1`: trust, tampering, expiry, sequence/channel/flavor,
  unsafe URLs/paths, installed binary verification and persistent preferences.
- `tools/test_installer_support.ps1`: preservation, transaction recovery,
  ownership, product isolation and registered-root checks using fixtures.
- `tools/test_installer_identity.ps1`: clean/dirty/source identity behavior.
- `VideoProcessorConfigTests.exe --test "update shutdown discards edits without prompt"`:
  actual repeated native shutdown requests against hidden clean/dirty Config
  windows, with no prompt or writes to the temporary configuration. Fixtures are destroyed before their queued application exit.
- `tools/test_portable_update.ps1 -IsccPath <ISCC>`: real full/Config-only
  installer runs against dummy portable folders with unique QA identities; checks
  replacement, settings preservation and absence of registration/shortcuts.
- Compile the full x64 Release solution, the helper, and both Inno flavors.

Release qualification still needs a disposable-machine end-to-end download,
setup replacement, restart, failure/recovery and second-computer compatibility
run using published signed assets. No live apps are stopped by these unit tests.
Cache downloads and logs are retained under
`%LOCALAPPDATA%\VideoProcessor\Updates` for diagnosis; automatic cache pruning is
not yet implemented.
