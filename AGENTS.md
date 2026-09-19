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

## Testing and diagnostics

- For VideoProcessor testing and troubleshooting, inspect the debug log at
  `C:\logs\vp.log`.

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
