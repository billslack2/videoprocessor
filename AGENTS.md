# VideoProcessor repository guidance

## Source and GitHub

- There is no authoritative local source checkout. Preserve existing checkouts
  and worktrees; do not use their branch or HEAD as the integration baseline.
- At the start of source work, query `billslack2/videoprocessor` on GitHub,
  discover and fetch the latest beta integration branch, and start at its current
  remote tip in a clean worktree under `E:\codex\videoprocessor`. Use
  `E:\codex\story-tracker` for tracker worktrees. Do not create new worktrees
  on C: or inside an existing checkout. Use a different base only when requested.
- `billslack2/videoprocessor` is the only GitHub repository of record. This is
  a permanent independent fork; never create, retarget, or merge pull requests
  against `enchywastaken/videoprocessor`.
- Before branching, creating a PR, or merging, discover the current GitHub
  default branch with:
  `gh repo view billslack2/videoprocessor --json defaultBranchRef --jq .defaultBranchRef.name`
  Verify it against the discovered current beta; do not substitute `main` or a
  stale local branch for the beta integration baseline.
- Verify the active checkout, branch, remote, and worktree status before Git
  writes. Feature branches start from the current remote beta tip.

## Stories

- Stories are tracked in `C:\Users\bslac\vp\story-tracker\stories`.
- Manage story-state changes from that tracker on `main`, not from this source
  checkout. Synchronize with `origin/main` before editing and commit/push each
  completed state change so the tracker remains authoritative.

## Local x64 Release builds

- The official MWCapture SDK headers are installed at
  `E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3`. For builds from
  isolated worktrees, pass `/p:MagewellSdkRoot=E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3`
  to MSBuild; the worktree-relative `3rdparty\magewell\SDKv3` path is not
  populated on this workstation. Confirm
  `Include\LibMWCapture\MWCapture.h` exists there before treating a build
  failure as a missing SDK. Keep the SDK outside source worktrees.
- Use `pwsh -NoProfile -File tools/build_release.ps1` from the source root.
  Every source-built project uses v143 / MSVC **14.44.35207**, including the
  player, static libraries, both renderers, MFC, Qt config and diagnostic tools.
  Install the matching x64 MFC/ATL component alongside the compiler. The wrapper
  selects one VS 2022-or-newer MSBuild host for the entire build.
  `-IncludeTests` also builds both test projects; it does not run tests.
  Use `-Phase runtime` or `-Phase config` to build only the affected group.
  `-PlanOnly` prints the selected hosts/targets without building.
- Do not choose a bare `MSBuild.exe` from PATH or override the repository's
  `VCToolsVersion`/`PlatformToolset`. `Directory.Build.props` pins the compiler;
  `Directory.Build.targets` rejects mismatched projects before compilation.
  The wrapper clears inherited compiler/library overrides, selects x64 host
  tools, disables node reuse, serializes its invocations, and preserves logs
  and exit-code receipts under `artifacts/release-build`. Do not concurrently
  run manual builds against the same worktree/intermediate directories.
- Normal builds are incremental. If LNK1103 recurs, stop and inspect the first
  failing phase's log and actual compiler/linker/library paths; do not repeatedly
  retry the entire solution. `-Rebuild -IncludeTests` is required once when
  migrating old v142 intermediates to the unified compiler. A narrower
  `-Phase runtime -Rebuild` can recover incompatible runtime intermediates after
  a toolchain change. Re-run the same incremental command afterward to verify
  recovery. Do not disable debug information or change rendering code to hide it.
- LNK1103 was observed during the former mixed v142/v143 build setup. Its exact
  original corruption mechanism was not established. Do not reinstate the split
  or treat repeated clean rebuilds as a permanent fix.

## Testing and diagnostics

- After changing temporal packet lifetime, source processing, or subtitle GPU
  preparation, run `tools/test_subtitle_motion_renderers.ps1` against both shipped
  libplacebo dependencies in addition to the relevant native/stack regressions.
  The default test target covers only the standard dependency. See
  `docs/VP-0207_SUBTITLE_MOTION_REGRESSIONS.md` for the command and playback limits.

- For VideoProcessor testing and troubleshooting, inspect the active runtime log at
  `C:\Videoprocessor\vp\logs\vp.log` when launched from the deployment directory.
  Confirm its modification time matches the running process; older sessions may
  have used `C:\logs\vp.log`. Launch the deployed player with working directory
  `C:\Videoprocessor\vp` so logs and relative configuration paths resolve there.

- For an exact native subtitle reproduction, optionally set the child process
  environment variable `VP_SUBTITLE_CAPTURE_PATH` to an absolute scratch-file
  path before launching VP. The subtitle worker writes one native
  frame (including unavailable picture/subtitle detection; zero picture bounds
  in the sidecar must be supplied from independently observed source geometry) and a `.txt` sidecar (dimensions, strides, format, encoding, color space,
  picture bounds, script hint). Clear the variable for ordinary launches; do
  not commit captured video pixels. This does not change active configuration.
  If the suspected fault is the near-black classification itself, also set
  `VP_SUBTITLE_CAPTURE_ALLOW_DARK=1` for that diagnostic child only. It captures
  after 240 accepted frames so a dark paused picture is not vetoed by the
  classifier under investigation. Clear both variables for ordinary launches.

## Deployment

- Deploy to `C:\Videoprocessor\vp` only when asked.
- Always deploy from a successfully completed x64 Release build. Never deploy
  Debug binaries.
- Treat `VideoProcessor.exe` and
  `vprenderer\VideoProcessorVPRenderer.dll` as one versioned runtime pair.
  Every VP deployment must back up and replace both files from the same x64
  Release build/commit, even when the implementation change appears confined
  to the host executable. Verify that both deployed hashes match their build
  artifacts before declaring the deployment complete.
- Treat deployed configuration as user data. Before editing any `.cfg`, `.ini`,
  `.json`, or state file, make a timestamped backup in the deployment folder.
- Preserve existing values, comments, and unknown keys. Add or modify only the
  exact entries required for the requested functionality; do not replace an
  entire configuration file with a source-tree sample.
- Report the backed-up paths and the exact deployed configuration edits.

## Release packaging and runtime prerequisites

- VP-0192 setup and its optional portable ZIP are built with
  tools/build_installer.ps1 after a successful x64 Release solution build.
  They use app-local Microsoft runtime DLLs from licensed Visual Studio Release
  redist folders. Verify x64 architecture, Microsoft signatures, dependency
  closure and per-library version floors. Never copy DLLs from Windows/System32,
  run a system-wide runtime installer, or create a desktop shortcut.
- The older tools/package_release.ps1 produces a central-runtime ZIP and is
  still used as a validated staging input by the new builder. Its -VcRedistPath
  (or VP_VC_REDIST_X64) requires the official signed x64 redistributable;
  this EXE is not included or executed by VP-0192 setup or its portable export.
- Preserve `Directory.Build.targets` runtime records when collecting build
  outputs. Packaging verifies their binary hashes; missing or stale records
  require a rebuild. Do not fabricate records to get past a packaging failure.
- Review `packaging/release-manifest.json`'s `vcRuntime.minimumVersion` whenever
  compiler headers, Qt, or other binary dependencies change. PE linker versions
  only identify a family; the policy floor must cover dependency patch versions.
- The legacy tools/package_release.ps1 ZIP alone includes START-HERE.txt,
  SETUP-RUNTIME.cmd and prerequisites/. Do not apply that legacy distribution
  contract to VP-0192's app-local installer or optional portable ZIP.
- Run the installer support, identity and real-installer lifecycle tests.
  Clean Windows qualification without Visual Studio must verify app-local
  dependency loading with absent/old global VC runtimes, then edit, Apply, OK
  and reopen Config. Record any untested VM/interactive coverage explicitly.
- See docs/VP-0192_INSTALLER.md for the current build and runtime contract;
  docs/VP-0107_RELEASE_LAYOUT.md documents the legacy packager.

## Portable sample configuration contract
- Every portable ZIP must contain root VideoProcessor.cfg.example, nonempty and
  byte-identical to the source VideoProcessor.cfg, recorded as a seed in its manifest.
- Never include an active VideoProcessor.cfg in the ZIP or replace an operator's
  saved configuration. For a fresh folder only, copy the example to VideoProcessor.cfg.
- Run tools/test_portable_config_contract.ps1 and validate each finished archive
  with tools/test_portable_config.ps1 before exporting or sharing it.

## Published release assets

- Publish installer EXEs, portable ZIPs, the optional manually applied
  `docs/edid/vertex2-lg-dv30-hdr10bit.bin`, and the required signed
  `vp-update.json` for automatic updates. Never apply the EDID automatically.
- Do not upload SHA/checksum files, manifest sidecars, build receipts, or other
  JSON evidence. Keep hashes and qualification records internal.
