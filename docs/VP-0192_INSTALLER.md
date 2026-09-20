# VP-0192 Windows installer

## Install and update

Download the selected x64 Setup executable from
[billslack2/videoprocessor Releases](https://github.com/billslack2/videoprocessor/releases).
Run it normally, **not as administrator**. Close VP and save/close Config,
including its tray icon, before continuing. Setup never force-terminates them.

Fresh installs default to your local Programs\VideoProcessor directory.
The destination page allows selection of a writable folder. To adopt an existing
ZIP installation, select its actual root (for example C:\Videoprocessor\vp),
not its parent. Setup registers that location for this Windows user. Subsequent
installers automatically reuse it, regardless of where the downloaded installer
is stored. A conflicting /DIR is refused once registered. To relocate, uninstall
(data is retained), move the preserved folder, and select it on reinstall.
This is one per-user installation; other accounts have separate registrations.

VP stores configuration/state beside the executable, logs under logs, and the
renderer cache under vprenderer. Setup tests write access without elevation and
does not grant broad permissions. Protected destinations need to be moved to a
user-writable location. No application storage or behavior changes are included.

Setup is self-contained and works offline. Microsoft-signed x64 Visual C++
runtime DLLs are installed beside VP and in config/vprenderer as required by
their dependencies. Setup never executes a system-wide redistributable, asks
for runtime elevation or creates a desktop icon. Windows 10/11 supplies the
Windows/UCRT components. Start menu shortcuts and one per-user uninstall/location
registration are the only integration outside the chosen application folder.

Deleting the application folder removes its application and private runtimes,
including any user data in that folder. The Windows uninstall entry and Start
menu shortcuts remain until uninstall or repair. Rerunning setup repairs them
and recreates a deleted folder at its remembered location. It cannot recover
user data that was manually deleted.

Core version and Git commit identify the selected build. Setup accepts the same
build again, a different commit at the same version, and older builds. Older
applications may not understand newer settings; setup never rewrites them.

## File ownership and recovery

INSTALL-MANIFEST.json is the small ownership record needed for safe future updates.
Application binaries and user guides are replaced even when version resources
are equal or older. Existing VideoProcessor.cfg, state, profiles, custom
shaders/LUTs, logs, caches and other unowned data are retained. Missing default
config/shaders are seeded and retained by uninstall. No configuration migration
is needed, so setup does not edit or back up configuration.

The installed application contains its runtime dependencies, default shaders,
the configuration HTML/NLS PDF user guides, and required licenses/provenance.
The repair helper runs only from setup's private temporary directory, which
Inno removes on exit. No redistributable installer is embedded or executed. The application folder receives no
prerequisites or setup directory, ZIP setup instructions, development/story
notes, release-layout docs, portable release inventory, or duplicate cfg.example.

Updates retire these extras from earlier installers only when the previous
manifest's hash still matches. Edited docs/examples and user-added files are
preserved; folders containing them remain. ZIP adoption uses its release inventory
for old binaries, known package hashes for textual extras, and verified generated
runtime metadata. Unrecognized ZIP extras are preserved. Cleanup never uses a
wildcard to delete docs, backups, configuration, or state.

Before replacement, setup verifies a temporary application-file backup under
.vp-installer-backups. Missing, old or corrupted managed binaries and runtime
DLLs are replaced from the selected package. A missing or damaged install
manifest is rebuilt without changing existing configuration/state.
Unknown DLLs in application load paths and modified obsolete binaries are
preserved under recovered-files/<transaction>/<original-path> before removal
from their old load paths. Review these files if you used custom binary plugins.
This recovery folder is retained by uninstall; routine successful updates
do not leave application backup directories behind.

A pending transaction is restored when setup is retried after interruption.
Normal failure also attempts restoration. Do not launch VP after interruption
until setup completes. Recovery affects application files only. Recovery backups
remain after failed/interrupted setup so the next installer can recover them.
After successful setup, verified complete/restored backups from this and earlier
attempts are removed, followed by empty setup-only directories. Unknown, modified,
or incomplete recovery contents are retained. Allow space for the package and
one temporary copy of the old application.

To recover, close VP/Config and rerun an intact installer. It extracts its own
helper and restores pending application-file transactions before updating.
To roll back a completed installation, run the chosen older installer; permanent
copies of every previous build are not kept. Never restore operator
configuration/state from an application-file backup.

Use **Uninstall VideoProcessor** in the application folder or Start menu, or
Windows Settings > Apps. The shortcut points to the registered uninstaller.
Inno's paired uninsNNN.exe/uninsNNN.dat files retain their internal names so
upgrades and rollbacks can reuse the uninstall history. They are marked hidden
(including an optional language .msg file); do not rename or remove them.
The DAT is required uninstall bookkeeping, not user configuration.

If Config is running, uninstall explains how to save changes and choose **Exit**
from its tray icon near the clock. Closing Config's window only hides it.
Choose **Retry** after exiting, or **Cancel** to leave the installation intact.
The message names VP, Config, or both, depending on what is running. Suppressed
silent prompts cancel safely. No process is force-terminated.

Uninstall removes tracked application files, shortcuts and its one registration.
It retains operator data and the small install manifest. Private runtime DLLs are removed with the application; no system runtime is changed. Reinstall into the preserved folder to retain settings.

## Repeatable release build

Use Windows 10/11 x64, Visual Studio with the repository's v142/v143 C++ and MFC
toolsets and Windows SDK, Qt 6.8.3 (scripts/bootstrap-config-editor-qt.ps1), and
Inno Setup **6.7.3**. Obtain the compiler from the official
[download page](https://jrsoftware.org/isdl.php) and verify its publisher signature
(Pyrsys B.V.) and official checksum. The compiler installer SHA-256 used here is
9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732.

For a release, use a clean checkout of the selected source commit:

~~~powershell
.\tools\build_installer.ps1 -CoreVersion '1.3.005-beta' -VcRedistPath 'C:\path\vc_redist.x64.exe' -IsccPath 'C:\path\ISCC.exe' -PortableZip
.\tools\test_installer_support.ps1
.\tools\test_installer_identity.ps1
.\tools\test_runtime_packaging.ps1 -VcRedistPath 'C:\path\vc_redist.x64.exe'
~~~

The command completes a full x64 Release solution rebuild, records the exact
commit, source fingerprint and four VP binary hashes, invokes the runtime-aware
manifest packager, resolves private runtime dependencies with dumpbin, then
compiles setup. Licensed Visual Studio Release CRT/MFC redistributable folders
are discovered automatically; -RuntimeDirectories may specify them explicitly.
Runtime copies are checked for x64 architecture, valid Microsoft signatures,
hashes and the recorded toolset/policy minimums. MFC uses its consumers' MFC
toolset floor; CRT must satisfy the newest shipped dependency's floor.
-VcRedistPath is currently a build-only input required by the legacy staging
validator. Its EXE and central-runtime helper are excluded from distribution. -SkipBuild requires the same successful
build receipt, exact source contents/dirty status and binary hashes. Source changes
during build or packaging reject the result. Never fabricate a receipt.

Setup and uninstall use images/VideoProcessor.ico, the player's existing icon.
Clean installer filenames are VideoProcessorSetup-<version>-<commit-sha12>.exe, for example
VideoProcessorSetup-1.3.005-beta-<commit-sha12>.exe. Dirty worktrees are supported for test builds:
VideoProcessorSetup-<version>-<commit-sha12>-dirty-<source-sha256-prefix12>.exe.
The fingerprint includes tracked source contents/deletions and untracked files;
ignored build outputs do not affect it. A commit SHA alone cannot identify
uncommitted changes. Full identity is recorded in INSTALL-MANIFEST.json and the
build receipt; setup and Windows file properties show the version and build label.
Clean builds of the same version use distinct commit-bearing filenames; archive
them in separate build directories when retaining multiple test builds.

Outputs under artifacts\installers include SHA-256 sidecars for the exact
executable bytes. Optional ZIPs contain the same self-contained application and
private runtime DLLs, with VideoProcessor.cfg.example instead of an active
configuration file, and keep their version/build-labelled filenames. No runtime
setup is needed. Copy/rename the example only when creating a new configuration. Publish clean, qualified builds.

The installer is currently **unsigned**. A checksum verifies integrity, not
publisher identity; Windows may show unknown-publisher/SmartScreen prompts.
Before public distribution, obtain a publisher Authenticode certificate and sign
application binaries and setup/uninstaller through a reviewed build signing step,
then regenerate hashes. This implementation does not claim signing.

Inno Setup's [license](https://jrsoftware.org/files/is/license.txt) was reviewed
2026-09-19: preserve its required copyright notices and website addresses.
The official site requests purchase for commercial use. Review the included
license against the project's distribution context. Retain VP's LICENSE.txt and
all third-party notices. Qt, libplacebo and other LGPL components retain their
own redistribution/source obligations; packaging alone does not discharge them.
Use Microsoft's official redistributable or the one from licensed Visual Studio.

After qualification, manually upload artifacts to billslack2/videoprocessor
Releases. Include commit, runtime minimum, signing status, supported Windows
versions, test evidence and rollback compatibility limits. This command does
not publish or deploy.

## Qualification matrix

Run real installers on disposable Windows without Visual Studio.
Developer-machine or helper-fixture success does not qualify clean-machine runtime loading.

| Case | Required evidence |
| --- | --- |
| Missing / old / sufficient global runtime | Offline setup without elevation or system changes; local module paths; Config edit, Apply, OK, reopen |
| Fresh custom path | Non-elevated launch, config save and log writes |
| A -> B (same core version) -> B -> A | Selected hashes, matching host/renderer, one entry, automatic path reuse |
| ZIP adoption / previous full installer | Comments, state and custom assets byte-identical; obsolete owned DLL and setup-only files retired; edited docs retained |
| Clean installed payload | No prerequisite/setup tools, duplicate example or developer notes; guides/licenses retained; verified recovery backups pruned after success |
| Unknown DLL / modified obsolete file | Byte-identical copy in recovered-files; obsolete load path cleared |
| VP / Config tray / unsaved editor | Save/close/retry; cancellation retains old files |
| Deleted folder / missing or corrupt manifest / stale runtime DLL | Remembered path repaired; existing operator data unchanged |
| Interruption / disk or hash failure | Backup recovered; complete old or selected payload |
| Uninstall / reinstall | Operator data retained; private runtime removed/reinstalled; usable config |

For automated lifecycle checks on an account without an existing VP installer
registration (the test creates and removes a disposable registered installation):

~~~powershell
.\tools\test_installer_e2e.ps1 -InstallerPath '<build-B-Setup.exe>' -PayloadRoot '.\artifacts\release\VideoProcessor' -PreviousInstallerPath '<build-A-Setup.exe>' -PreviousManifestPath '<saved-build-A-INSTALL-MANIFEST.json>'
.\tools\test_installer_zip_adoption.ps1 -InstallerPath '<build-B-Setup.exe>' -PortableRoot '<previous-portable-tree>' -PayloadRoot '.\artifacts\release\VideoProcessor'
.\tools\test_installer_failure.ps1 -PayloadRoot '.\artifacts\release\VideoProcessor' -IsccPath '<ISCC.exe>'
~~~

The optional previous-build inputs exercise A -> B -> B -> A at one core version.
The failure test builds an intentionally invalid QA-only executable. Never
distribute anything named DO-NOT-DISTRIBUTE. Tests refuse an existing registered
installation and keep their evidence under artifacts.

Record OS builds, privilege level, runtime versions, installer hashes and logs.
Missing clean-machine or interactive coverage remains an acceptance gap.
