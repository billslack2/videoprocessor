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

The bundled, Microsoft-signed x64 VC++ runtime installs offline when system files
do not meet the build's generated minimum. Only that prerequisite asks for
elevation. Sufficient runtimes are left alone. Installer users do not run
SETUP-RUNTIME.cmd; ZIP users still do. Denial/failure stops before application
replacement; a prerequisite reboot stops setup and requires rerunning it after
restart. Setup never automatically restarts Windows or launches the player.

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
The redistributable and setup scripts run only from setup's private temporary
directory, which Inno removes on exit. The application folder receives no
prerequisites or setup directory, ZIP setup instructions, development/story
notes, release-layout docs, portable release inventory, or duplicate cfg.example.

Updates retire these extras from earlier installers only when the previous
manifest's hash still matches. Edited docs/examples and user-added files are
preserved; folders containing them remain. ZIP adoption uses its release inventory
for old binaries, known package hashes for textual extras, and verified generated
runtime metadata. Unrecognized ZIP extras are preserved. Cleanup never uses a
wildcard to delete docs, backups, configuration, or state.

Before replacement, setup verifies a temporary application-file backup under
.vp-installer-backups. Obsolete binaries require an earlier inventory and matching
hash; a modified obsolete binary blocks setup. Without a release/install inventory,
unknown DLLs are reported by path, never guessed or deleted. Move a reported
private DLL to a separate backup folder only after reviewing it.

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

Uninstall removes tracked application files, shortcuts and its one registration.
It retains operator data and the small install manifest. It never uninstalls the
shared runtime. Reinstall into the preserved folder to retain settings.

## Repeatable release build

Use Windows 10/11 x64, Visual Studio with the repository's v142/v143 C++ and MFC
toolsets and Windows SDK, Qt 6.8.3 (scripts/bootstrap-config-editor-qt.ps1), and
Inno Setup **6.7.3**. Obtain the compiler from the official
[download page](https://jrsoftware.org/isdl.php) and verify its publisher signature
(Pyrsys B.V.) and official checksum. The compiler installer SHA-256 used here is
9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732.

From a clean checkout of the selected source commit:

~~~powershell
.\tools\build_installer.ps1 -CoreVersion '1.3.005-beta' -VcRedistPath 'C:\path\vc_redist.x64.exe' -IsccPath 'C:\path\ISCC.exe' -PortableZip
.\tools\test_installer_support.ps1
.\tools\test_runtime_packaging.ps1 -VcRedistPath 'C:\path\vc_redist.x64.exe'
.\artifacts\release\VideoProcessor\SETUP-RUNTIME.cmd -CheckOnly
~~~

The command completes a full x64 Release solution rebuild, records the exact
commit and four VP binary hashes, invokes the runtime-aware manifest packager,
then compiles setup. -SkipBuild requires the same successful build receipt,
unchanged commit and hashes. Never fabricate a receipt.
Outputs under artifacts\installers include version/commit-labelled executables
and SHA-256 sidecars. Optional ZIPs use the original portable layout.

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
Developer-machine or helper-fixture success does not qualify prerequisites.

| Case | Required evidence |
| --- | --- |
| Missing runtime / old 14.29 / sufficient runtime | Offline install or skip; Config edit, Apply, OK, reopen |
| Fresh custom path | Non-elevated launch, config save and log writes |
| A -> B (same core version) -> B -> A | Selected hashes, matching host/renderer, one entry, automatic path reuse |
| ZIP adoption / previous full installer | Comments, state and custom assets byte-identical; obsolete owned DLL and setup-only files retired; edited docs retained |\n| Clean installed payload | No prerequisite/setup tools, duplicate example or developer notes; guides/licenses retained; verified recovery backups pruned after success |
| Unknown DLL / modified obsolete file | Named conflict, no destructive replacement |
| VP / Config tray / unsaved editor | Save/close/retry; cancellation retains old files |
| UAC denial / prerequisite error / restart | No successful update or launch before runtime ready |
| Interruption / disk or hash failure | Backup recovered; complete old or selected payload |
| Uninstall / reinstall | Data and shared runtime retained; usable config |

For automated lifecycle checks on an account without an existing VP installer
registration (the test creates and removes a disposable registered installation):

~~~powershell
.\tools\test_installer_e2e.ps1 -InstallerPath '<build-B-Setup.exe>' -PayloadRoot '.\artifacts\release\VideoProcessor' -PreviousInstallerPath '<build-A-Setup.exe>' -PreviousManifestPath '<saved-build-A-INSTALL-MANIFEST.json>'
.\tools\test_installer_failure.ps1 -PayloadRoot '.\artifacts\release\VideoProcessor' -IsccPath '<ISCC.exe>'
~~~

The optional previous-build inputs exercise A -> B -> B -> A at one core version.
The failure test builds an intentionally invalid QA-only executable. Never
distribute anything named DO-NOT-DISTRIBUTE. Tests refuse an existing registered
installation and keep their evidence under artifacts.

Record OS builds, privilege level, runtime versions, installer hashes and logs.
Missing clean-machine or interactive coverage remains an acceptance gap.\n