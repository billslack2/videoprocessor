# VP-0192: Windows installer with runtime setup and unrestricted build upgrades

## Status

Review

Self-contained runtime, repair, cleanup, branding and uninstall UX are ready for
review in [draft PR #99](https://github.com/billslack2/videoprocessor/pull/99).
Source commit: 4ed2980a8368a96ae3ef931e6a2f5fbecb0895ff, pushed on
codex/vp-0192-windows-installer. Integration base/default verified as
v1.3.005-beta at 73c850041d284e3e37c959a8956d896b79106aa1.
Worktree: E:\codex\videoprocessor\vp-0192-windows-installer.

The user's revised requirement supersedes central-runtime installation:
private CRT/MFC DLLs, no system-wide runtime setup, no desktop icon, and repair
of deleted/corrupt/old installs while preserving surviving config/state.
The final production installer updated the authorized C:\vptest installation
at its remembered path; all managed hashes and nine preserved files verified.
C:\Videoprocessor\vp was not modified.

Clean-Windows and interactive qualification remain open. This is ready for
review, not complete or publicly released. Earlier prototype evidence below
is historical; the final self-contained evidence is at the end.

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
This story reuses its toolset/signature/hash checks, but the user now requires
app-local DLL deployment instead of a system-wide prerequisite. Qualification
must establish local runtime loading on a clean machine.

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
- Bundle Microsoft-signed x64 Release CRT/MFC DLLs beside each consumer, using
  official Visual Studio redistributable files and toolset-derived floors.
  Never run a system-wide runtime installer. Setup and its portable export
  must work offline without a separate runtime setup step.
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
  Handle interrupted/failed application installation with clear outcomes and
  recovery instructions. Private runtime deployment needs no elevation or reboot.
  Do not report success or launch VP before its dependencies/files are ready.
- Provide Start menu shortcuts, a friendly local uninstall shortcut and one
  uninstall entry. Never create a desktop shortcut. Uninstall preserves operator
  data and removes private runtime DLLs without touching system runtimes.
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

1. On clean supported x64 Windows with missing, old 14.29 and sufficient global
   VC++ runtimes, install offline without elevation or system runtime changes.
   Verify VP/Config use private runtime DLLs, and Config Apply/OK/reopen persist
   settings without the reported mutex crash.
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
   delete unknown private DLLs; preserve them in a recovery folder outside load paths.
5. Exercise running VP, a background Config GUI, and unsaved editor changes.
   Cancellation leaves the previous installation usable; consented shutdown
   permits replacement without losing configuration.
6. Repair missing/corrupt app/runtime files and install metadata, including a
   manually deleted folder with retained registration. Reuse its path and
   preserve surviving config/state. Exercise installer interruption/hash failure
   with truthful status and recovery. Validate actual installers, not only mocks.
7. Uninstall removes owned application files, shortcuts, and registration,
   including private VC runtime DLLs, while preserving user data. Reinstallation into
   the preserved directory retains usable settings.
8. From a clean checkout of the selected integration commit, the documented
   Release packaging command generates a version/build-labelled installer
   and checksum suitable for GitHub upload. Verify installed payload hashes
   against that build and record exact test environments and results.
9. No install, upgrade or repair creates a desktop shortcut.
10. A user can download the next installer from GitHub and run it to replace
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

## Initial central-runtime prototype evidence (superseded, 2026-09-19)

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
clean supported Windows without Visual Studio, app-local loading with missing
and old global runtimes,
Config edit/Apply/OK/reopen persistence, unsaved-editor interaction, and forced
interruption of a real installer (helper-journal interruption has passed).
Player/capture hardware startup and public GitHub download qualification have not
been exercised here. Current-machine success does not substitute for those cases.

Tracker audit: 211 indexed items match one canonical state folder each, with no
duplicate IDs or orphan index rows. Historical noncanonical Status formatting
elsewhere (including VP-0176 lacking the standard heading) was left unchanged.
Only VP-0192 and its index row changed state in this work.

## Earlier cleanup and installer identity evidence (2026-09-19)

Implemented in source commit b6a351c83ce499c0acf12df4eaf38f928f7b6c32,
including cleanup commit 42ab23d3dbcf8ae20fb429558bf3794cd1f1cb59 and branding
commit 7a7aa553e4c387da3c9a95a080cb92cda5c3b2c5. Draft PR #99 remains based
on v1.3.005-beta. Source branch is pushed and clean.

- Installed application payload is reduced to 56 files plus its small ownership
  manifest/uninstaller. Thirteen setup/ZIP-only files are excluded: prerequisite
  tools, recovery scripts/docs, development notes, release layout/inventory, and
  duplicate config example. Prerequisites are embedded/extracted temporarily;
  user guides, required licenses and runtime dependencies remain installed.
- Older installers/ZIPs have recognized extras retired safely. Edited docs,
  user backups and unknown files are preserved. Completed/restored recovery
  backups are hash-verified and pruned only after successful setup. Pending,
  incomplete, altered or unrecognized recovery contents remain protected.
- User selected versioned filenames: VideoProcessorSetup-<version>.exe for clean
  builds. Dirty builds add <commit-sha12>-dirty-<source-fingerprint12>.
  The fingerprint covers tracked contents/deletions and untracked source files;
  ignored build outputs do not affect it. Build receipts check the exact source
  snapshot and dirty state as well as all four Release binary hashes.
- Setup and uninstall use the exact existing images/VideoProcessor.ico asset.
  Windows File Version shows the core version; Product Version and setup display
  the complete selected version/build label.
- Final x64 Release solution build passed: 0 errors, 47 existing warnings.
  Clean and dirty installer compilation passed with Inno Setup 6.7.3.
- 41 preservation/recovery checks, 23 prerequisite regressions and 11 source
  identity checks passed. A real stale dirty-source build receipt was rejected.
- 122 actual-installer lifecycle checks passed for old full-layout build
  4391c7cbbbf4 -> cleaned build 42ab23d3dbcf -> same build -> older build.
  These include exact managed hashes, one registration, automatic custom-path
  reuse, Config process refusal, unchanged config/state/assets, absence of the
  13 excluded files and temporary folders, and uninstall/reinstall.
- Two real older-ZIP cleanup cases passed: pristine extras removed; edited
  development notes and user-added prerequisite/setup files preserved.
  Existing config/state/profile/shader/LUT/log files remained byte-identical.
- Lifecycle, ZIP and failure harnesses used a separate QA registration and
  shortcut identity because the user had installed the previous build in
  C:\vptest. Payloads and helper behavior were unchanged; QA registrations
  were removed at test completion.
- User explicitly approved updating C:\vptest. The actual production installer
  first updated 4391c7cbbbf4 -> 42ab23d3dbcf, then the final production installer
  updated it to b6a351c83ce4 without /DIR. Both reused the registered path,
  matched all managed hashes, and retained its config plus seven shaders
  byte-for-byte. No state file existed there; state preservation was tested in
  the lifecycle/ZIP fixtures. Prerequisites, setup and installer backup folders
  were removed. C:\Videoprocessor\vp was not modified.
- Exact icon-resource checks passed for all five VP icon images in the final
  setup executable and C:\vptest\unins000.exe. File/Product Version metadata
  matched the selected version/commit. Final corrupted-manifest setup failed
  truthfully, restored old application files, preserved config/state and created
  no QA registration.

### Current artifacts and evidence

Artifact root: E:\codex\videoprocessor\vp-0192-windows-installer\artifacts

- installers/VideoProcessorSetup-1.3.005-beta.exe
- SHA-256: 2AB1B1F0C1D53E514D72D79292D75FFC500050A21903F76CEA8F5CFF58792A64
- Matching .sha256 sidecar and portable ZIP for source b6a351c83ce4.
- Dirty-build naming evidence:
  installers/VideoProcessorSetup-1.3.005-beta-42ab23d3dbcf-dirty-ec921b919cb7.exe
  (precommit source snapshot recorded in installer-dirty-manifest.json and
  installer-dirty-build-receipt.json; test artifact, not a release).
- installer-final-build.log, installer-final-icon.log, installer-final-failure.log
- installer-cleanup-support.log, installer-cleanup-runtime.log,
  installer-cleanup-checkonly.log, installer-cleanup-e2e.log, installer-cleanup-zip.log
- installer-identity-tests.log, installer-stale-source.log, installer-dirty-build.log
- registered-cleanup-before.json, registered-cleanup-setup.log,
  registered-cleanup-result.log; registered-final-before.json,
  registered-final-setup.log, registered-final-result.log

The earlier remaining acceptance/unsigned-release limitations still apply.
These changes are ready for review, not a public release. No GitHub Release
was published. Historical tracker Status-format anomalies were left unchanged.


## Final self-contained runtime, repair and uninstall follow-up (2026-09-19)

- Source commits 0ba2d0fa and 4ed2980a; final clean source/build:
  4ed2980a8368a96ae3ef931e6a2f5fbecb0895ff.
- Seventy installed payload files include fourteen private Microsoft runtime
  DLL copies in host/config/renderer folders. CRT version 14.51.36247.0 meets
  minimum 14.44.35211.0; MFC 14.29.30139.0 meets its consumers' v142 toolset
  floor 14.29.30133. Architecture, Microsoft signatures, dependency closure
  and copy hashes are checked from licensed Visual Studio Release redist files.
- Setup and its optional portable ZIP contain no central-runtime installer.
  The older packager's signed EXE input is build-time validation only.
  No desktop task/icon exists. Start menu shortcuts and the per-user uninstall/
  remembered-location registration remain outside the chosen folder.
- Missing/corrupted managed DLLs and manifests are repaired. Unknown private
  DLLs and changed obsolete binaries are copied to recovered-files before
  clearing their original load paths. Manual deletion leaves registration;
  rerunning setup recreates the remembered directory. Deleted operator files
  cannot be recovered automatically; surviving configuration/state are preserved.
- Uninstall VideoProcessor.lnk is the friendly root/Start menu entry. The
  required Inno numbered EXE/DAT names remain internal and hidden, preserving
  native upgrade history. DAT is required bookkeeping and must not be renamed.
  Running-app messages identify VP/Config and explain Config tray Exit and
  Retry/Cancel. Silent refusal preserves registration/data.
- Clean x64 Release rebuild succeeded: 0 errors, 47 preexisting warnings.
  Inno Setup 6.7.3 compilation and self-contained portable ZIP generation passed.
- 49 helper preservation/recovery checks, 11 build-identity checks and three
  runtime rejection checks (missing source, old CRT, System32 source) passed.
- 278 real-installer checks passed with isolated QA registration/shortcuts:
  A/B/reinstall/rollback, remembered custom path, exact managed hashes, single
  registration, Config refusal, corrupted/missing runtime files and manifests,
  damaged uninstall DAT, unknown-DLL recovery, no desktop changes, uninstall/
  reinstall, and full manual folder deletion followed by repair.
  Config loaded all six observed VC++/MFC modules from its own config directory.
  Running-Config uninstall logged explicit tray Exit/Retry/Cancel instructions.
- The final rebuilt payload passed four real ZIP-adoption scenarios: legacy and
  app-local layouts, each with pristine extras and edited/user-added extras.
  Config/state/profile/assets/log hashes survived; obsolete DLLs were retired.
  A real injected payload-hash failure returned nonzero, restored the prior
  application, retained config/state, and created no QA registration.
- Final production installer upgraded C:\vptest from b6a351c83ce4 to 4ed2980a8368
  without /DIR. Every managed hash matched, and config, seven shaders and a log
  were byte-identical (nine files). No state file existed in C:\vptest; state
  preservation was exercised in lifecycle and ZIP fixtures. System-runtime
  registration remained unchanged. Unnecessary prerequisite/setup/backup folders
  were absent. Friendly shortcut and hidden native support files verified.
- All five exact VP icon images matched in setup and installed uninstaller.
  Source branch is pushed/clean. PR #99 remains draft. No release was published.

### Final artifacts

Root: E:\codex\videoprocessor\vp-0192-windows-installer\artifacts

- installers/VideoProcessorSetup-1.3.005-beta.exe
- SHA-256: 30D7D8D74CFD7F014601141C2A1B9EC2D00218CBE7314B8DF0D1F696DD22CBEE
- installers/VideoProcessor-1.3.005-beta-4ed2980a8368-x64-Portable.zip
- Matching checksum sidecars; installer-build.json records clean Release/x64.
- selfcontained-final-build.log, selfcontained-support.log,
  selfcontained-identity.log, selfcontained-runtime-negative.log
- selfcontained-e2e.log; installer-e2e-8253e85d824a41d69693bd58f20af301/
- selfcontained-legacy-zip.log, selfcontained-local-zip.log,
  selfcontained-final-failure.log, selfcontained-icon.log
- registered-selfcontained-before.json, registered-selfcontained-setup.log,
  registered-selfcontained-result.log, registered-selfcontained-ux.log

Still needed before full acceptance: supported clean Windows without Visual
Studio and with missing/old global runtimes; Config edit/Apply/OK/reopen and
unsaved-editor interaction; player/capture startup; forced interruption of a
real installer; public download/publisher-signing qualification. The artifact
is unsigned. Central-runtime elevation/reboot cases are superseded by the
user's explicit app-local requirement.
