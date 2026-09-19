# VP-0192: Windows installer with runtime setup and unrestricted build upgrades

## Status

Review

Implemented 2026-09-19 in [draft PR #99](https://github.com/billslack2/videoprocessor/pull/99),
source commit 4391c7cbbbf4105faaf95c051235c2b6a62d3835 on
codex/vp-0192-windows-installer. The verified integration base/default remains
v1.3.005-beta at 73c850041d284e3e37c959a8956d896b79106aa1.
Source worktree: E:\codex\videoprocessor\vp-0192-windows-installer.

The requested selectable installation location, later location reuse, existing
ZIP adoption, and preservation of configuration/state are implemented and tested.
The active C:\Videoprocessor\vp installation was not deployed or modified.
Review is pending the clean-Windows and interactive acceptance cases listed below;
this story is not complete and no public release has been published.

## User story

As a VP operator, I want to download one setup executable from GitHub and run
it for a fresh installation, upgrade, reinstall, or rollback, without manually
installing the VC++ runtime or losing my configuration.

## Background and dependencies

Andrew's Config GUI Apply/OK crash exposed a deployment dependency mismatch:
a newer-built editor used std::mutex with an older installed MSVC runtime.
The runtime packaging fix is merged in
[PR #98](https://github.com/billslack2/videoprocessor/pull/98)
(merge 73c850041d284e3e37c959a8956d896b79106aa1). The ZIP now includes a
validated redistributable, runtime requirement metadata, and a setup helper.
This story builds on that packaging contract and removes the separate helper
step for installer users. The earlier work did not validate real prerequisite
installation on a clean machine; this story must do so.

At implementation start, discover the current remote beta integration branch
in billslack2/videoprocessor and use its current tip in a clean source worktree
under E:\codex\videoprocessor, per the user's current source-work defaults.
Do not treat the historical merge above as the implementation baseline.

## Agreed scope

- Use Inno Setup to produce a single x64 VideoProcessor setup executable from
  a successfully completed Release build. Keep VP and its renderer DLL from
  the same commit and verify packaged hashes.
- Keep implementation in installer scripts, packaging/build tooling, release
  documentation, and installer tests. No player, Config GUI, or renderer
  application-code changes are planned. Record any discovered need to expand
  that boundary before changing application behavior.
- Bundle and invoke the official Microsoft x64 VC++ redistributable only when
  required. Reuse the existing toolset-derived minimum, signature/hash checks,
  and runtime readiness verification. Do not ask users to run SETUP-RUNTIME.cmd
  before using the installer. Offline prerequisite installation must work.
- Use one stable installation identity across normal and test builds. Discover
  the previous installation directory; support an explicitly selected existing
  ZIP installation, including the current C:\Videoprocessor\vp layout.
- Support fresh installation, upgrades, reinstalling the identical build,
  replacing a build with another at the same core version, and deliberately
  installing an older build. Do not impose a newer-version-only gate.
- Display the core version and Git commit/build identity in setup and give
  release assets distinguishable names. Test builds must not accidentally
  create duplicate uninstall entries.
- Replace all installer-managed application files with the selected package's
  files even when their version resources are equal or older. Scope replacement
  to owned application files; do not apply this policy to shared system runtimes.
  Remove obsolete managed files safely when necessary to avoid mixed builds.
- Preserve user configuration values, comments, profiles, custom shaders/LUTs,
  logs, and other unowned files. Install example/default configuration only
  when absent. Back up each configuration file before any required migration,
  and make the smallest required change.
- Resolve installation location and write permissions against VP's current
  configuration/log paths so ordinary operation does not require running VP
  elevated. Do not assume Program Files is suitable without validation.
- Detect VP and Config GUI/tray processes before replacing files. Provide a
  clear close/retry/cancel flow; do not silently discard unsaved editor work.
  Handle elevation denial, prerequisite failure, restart requirements, and
  interrupted installation with clear outcomes and recovery instructions.
  Do not report success or launch VP before its dependencies/files are ready.
- Provide appropriate launch shortcuts and one uninstall entry. Uninstall must
  preserve operator data and must not uninstall the shared Microsoft runtime.
  Define and test a recoverable application-file backup/restore procedure.
- Generate the installer through the repeatable release packaging process,
  alongside the optional portable ZIP. Document prerequisites, tool versions,
  license/redistribution obligations, build identity, checksums, and publishing
  steps. A checksum is not a replacement for publisher signing; record the
  signing approach or unsigned distribution limitation.
- Host manually downloadable installer assets on GitHub Releases for
  billslack2/videoprocessor. The user chooses a release/test build, downloads
  its installer, and runs it over the existing installation. No separate
  hosting service, subscription, or update backend is required.

## Out of scope

- In-app Check for updates, automatic polling/downloads, background updates,
  or a custom update service.
- Player, renderer, and Config GUI feature changes.
- Automatically publishing a release or installing a build during this
  story's planning phase.
- Promising backward configuration compatibility for arbitrary historical
  application versions. Allow selecting older builds without destructive
  configuration rewriting; document any actual compatibility limits.

## Acceptance criteria

1. On a clean supported x64 Windows test environment with no adequate VC++
   runtime, the single installer installs the bundled prerequisite and VP;
   Config GUI Apply and OK persist settings without the reported mutex crash.
   Repeat with an old 14.29 runtime and with an already sufficient runtime.
   The sufficient-runtime case performs no unnecessary runtime installation.
2. Fresh installation launches VP and Config GUI successfully, and normal
   configuration saves/log writes work without application elevation.
3. Install build A, then distinct build B with the same core version; reinstall
   B; then install older build A. Each operation is accepted, leaves one
   installation/uninstall entry, and produces the selected build's managed-file
   hashes, including the matching VP executable and renderer DLL.
4. Repeat an upgrade from an existing ZIP installation with representative
   user configuration/comments, custom assets, and an obsolete managed DLL.
   Preserve operator files, safely remove the obsolete owned file, and leave
   no stale DLL capable of recreating the runtime mismatch. Do not silently
   delete unknown private DLLs; resolve or clearly report conflicts.
5. Exercise running VP, a background Config GUI, and unsaved editor changes.
   Cancellation leaves the previous installation usable; consented shutdown
   permits replacement without losing configuration.
6. Exercise denied elevation, prerequisite failure/cancellation, installer
   interruption, and reboot-required outcomes. Verify truthful status and a
   documented recovery path without leaving an apparently successful mixed
   installation. Validate the actual installer, not only mocked helper calls.
7. Uninstall removes owned application files, shortcuts, and registration,
   while preserving user data and the shared VC++ runtime. Reinstallation into
   the preserved directory retains usable settings.
8. From a clean checkout of the selected integration commit, the documented
   Release packaging command generates a version/build-labelled installer
   and checksum suitable for GitHub upload. Verify installed payload hashes
   against that build and record exact test environments and results.
9. A user can download the next installer from GitHub and run it to replace
   the previous build without any application-side updater or paid service.

## Implementation readiness and validation record

Before implementation, inspect the current release manifest/runtime helpers,
configuration/log storage and ACL needs, process lifecycle, existing ZIP layout,
and managed-versus-user file ownership. Resolve those decisions in this story.
Review the current official Inno Setup license and compiler requirements at
that time. Record the selected base, branch/worktree, installer identity,
installation scope/path policy, recovery behavior, and test matrix.

Keep this story in Backlog until implementation begins. Move it and update
INDEX.md together at each state transition. Completion requires installer
execution evidence on clean and upgrade environments, the final implementation
commit/PR, and documented release generation; scripting alone is insufficient.

## References

- [Inno Setup](https://jrsoftware.org/isinfo.php)
- [Inno Setup license](https://jrsoftware.org/files/is/license.txt)
- [Inno Setup upgrade guidance](https://jrsoftware.org/isfaq.php)
- [GitHub Releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)

## Implementation decisions and evidence (2026-09-19)

- Compiler: verified official Inno Setup 6.7.3. License, redistribution obligations
  and the unsigned-distribution limitation are documented in
  docs/VP-0192_INSTALLER.md. VP's LICENSE.txt is now included in the release manifest.
- Stable AppId: {42D852F1-70E9-43ED-8739-D61752106D59}; one per-user registration.
  Default destination is the user's local Programs/VideoProcessor directory.
  The location page stays available for fresh/ZIP installs; later setup reuses
  the registered path and rejects conflicting redirection. Relocation is an
  explicit uninstall/preserve/move/reinstall procedure.
- Normal VP operation remains non-elevated. Setup rejects elevated execution,
  probes writable application/config/state/log/cache locations, and never grants
  broad ACL permissions. Only the required Microsoft runtime requests elevation.
- Application/Config/renderer source is unchanged. Existing runtime helpers and
  toolset/hash/signature checks are reused. Managed files use unconditional
  replacement; active config, state, custom assets and logs are not managed.
  Defaults and shipped shaders are seeded only when absent and retained on uninstall.
- Atomic backup journals preserve previous application files and recover pending
  attempts. Unknown private DLLs block with a named conflict; obsolete binaries
  require earlier inventory ownership. No configuration migration or edit occurs.
- Real failure injection found that an Inno script exception does not itself
  roll back registration. The final implementation verifies payload before
  shortcuts/registration, gates them on verification, returns a nonzero result,
  shows a failure completion page, and restores application files when setup exits.
- Reproducible command: tools/build_installer.ps1 -CoreVersion 1.3.005-beta
  -VcRedistPath <official-x64-redist> -IsccPath <Inno-6.7.3-ISCC.exe> -PortableZip.
  It requires a clean source commit and completed x64 Release solution rebuild,
  verifies four VP build hashes and stages via the existing release manifest.
- Final x64 Release rebuild passed (0 errors, 47 existing build warnings) with
  Visual Studio Professional 2026 18.7.3 and the repository's selected toolsets.
  Inno compilation and installer/portable ZIP checksum generation passed.
- Host: Windows 10 Pro 22H2, build 19045, ordinary user. Installed/bundled Microsoft
  runtime 14.51.36247.0; generated minimum 14.44.35211.0. Packaged CheckOnly passed.
- 20 preservation/recovery helper checks and 23 runtime prerequisite regression
  checks passed. Prerequisite failure/UAC/reboot scenarios in those regressions
  are mocked; they are not clean-machine installation evidence.
- 62 actual-installer lifecycle checks passed for A=73fe917d695d -> B=4391c7cbbbf4
  -> B -> A, both core version 1.3.005-beta. Every stage retained one registration,
  reused the custom location without /DIR on updates, and matched all managed
  hashes including the host/renderer pair. Real Config launched non-elevated in
  background mode, and setup refused to replace files while it was running.
  Config/state, an edited shipped shader, private shader/LUT/log and unowned file
  remained byte-identical through upgrade, reinstall, rollback, uninstall and reinstall.
- Actual corrupted-manifest installer returned failure, left no registration,
  restored previous application bytes, and preserved config/state.
- Actual ZIP adoption passed in a disposable custom directory: prior release
  inventory identified an obsolete DLL, which was retired; configuration/comments,
  state, profile JSON, shader, LUT and log hashes were retained. Managed payload
  hashes matched the selected package. Test registrations were removed afterward.
- Tests used disposable directories under source artifacts only. No runtime was
  changed on this host (the sufficient-runtime path was exercised).

### Artifacts and logs

Source artifact root:
E:\codex\videoprocessor\vp-0192-windows-installer\artifacts

- installers/VideoProcessor-1.3.005-beta-4391c7cbbbf4-x64-Setup.exe
- Setup SHA-256:
  31104E63EF74C6115A7A0B97345C720ADA2796EF5D62A127A4FBBD7EA9EC9EBA
- installers/VideoProcessor-1.3.005-beta-4391c7cbbbf4-x64-Portable.zip
  (with its .sha256 sidecar)
- installer-command.log, installer-build.log, installer-build.json
- installer-support-final.log, runtime-regression-final.log, installer-abba.log
- installer-e2e-4b7ce9caedbd466ca442d24f28eeedb6/
- installer-failure-final.log; installer-failure-6323d7027b1a4957a1201b650403633a/
- zip-adoption.log; zip-adoption-0c65a708aba640329bb8d445ccb01f72/

### Remaining acceptance and release decision

Keep the PR in draft until review/qualification is resolved. Still required:
clean supported Windows without Visual Studio, missing-runtime and old-14.29
offline installations, actual denied elevation/prerequisite failure/reboot cases,
Config edit/Apply/OK/reopen persistence, unsaved-editor interaction, and forced
interruption of a real installer (helper-journal interruption has passed).
Player/capture hardware startup and public GitHub download qualification have not
been exercised here. Current-machine success does not substitute for those cases.

Tracker audit: 211 indexed items match one canonical state folder each, with no
duplicate IDs or orphan index rows. Historical noncanonical Status formatting
elsewhere (including VP-0176 lacking the standard heading) was left unchanged.
Only VP-0192 and its index row changed state in this work.
