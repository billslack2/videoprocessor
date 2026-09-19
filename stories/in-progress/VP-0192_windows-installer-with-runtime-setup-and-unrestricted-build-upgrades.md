# VP-0192: Windows installer with runtime setup and unrestricted build upgrades

## Status

In Progress

Implementation started 2026-09-19. Remote beta/default branch verified as
`v1.3.005-beta`, tip `73c850041d284e3e37c959a8956d896b79106aa1`.
Source branch `codex/vp-0192-windows-installer`, clean worktree
`E:\codex\videoprocessor\vp-0192-windows-installer`.

The user explicitly requires selectable installation location, discovery and
reuse of the current location on subsequent updates, and preservation of all
existing state and configuration. Implementation and installer validation are
in progress; no deployment to the active VP installation is requested.

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
