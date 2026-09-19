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

INSTALL-MANIFEST.json records payload hashes and managed-versus-seed ownership.
Application binaries and documentation are replaced even when version resources
are equal or older. Existing VideoProcessor.cfg, its example, shaders, state,
profiles, custom LUTs, logs, caches and other unowned data are retained.
Missing defaults/shaders are seeded and retained by uninstall. No configuration
migration is needed, so setup does not edit or back up a configuration file.

Before replacement, setup verifies a timestamped application-file backup under
.vp-installer-backups. Retiring a file requires an older install manifest and a
matching old hash; a modified obsolete file blocks setup. For ZIP adoption,
the VP-0107 RELEASE-MANIFEST identifies old owned binaries. Without that inventory,
unknown DLLs are reported by path, never guessed or deleted. Move a reported
private DLL to a separate backup folder only after reviewing it.

A pending transaction is restored when setup is retried after interruption.
Normal failure also attempts restoration. Do not launch VP after interruption
until setup completes. Recovery affects application files only. Backups remain
after upgrade/uninstall; remove them manually once satisfied with the build.
Allow space for the package and one copy of the old application per attempt.

To restore an application backup manually with VP/Config closed:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'D:\VP\setup\install-support.ps1' -Action Restore -InstallRoot 'D:\VP' -BackupDirectory 'D:\VP\.vp-installer-backups\<timestamp-id>'
~~~

Rerun the corresponding older installer to reconcile registration and shortcuts.
If the helper is damaged, rerun an intact installer, which extracts its own helper.
Never restore configuration/state from an application-file backup.

Uninstall removes tracked application files, shortcuts and its one registration.
It retains operator data, the install manifest, recovery helper/docs and backup
history. It never uninstalls the shared runtime. Reinstall into the preserved
folder to retain settings.

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
| ZIP adoption | Comments, state and custom assets byte-identical; owned obsolete DLL retired |
| Unknown DLL / modified obsolete file | Named conflict, no destructive replacement |
| VP / Config tray / unsaved editor | Save/close/retry; cancellation retains old files |
| UAC denial / prerequisite error / restart | No successful update or launch before runtime ready |
| Interruption / disk or hash failure | Backup recovered; complete old or selected payload |
| Uninstall / reinstall | Data and shared runtime retained; usable config |

Record OS builds, privilege level, runtime versions, installer hashes and logs.
Missing clean-machine or interactive coverage remains an acceptance gap.
