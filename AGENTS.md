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

- Every distributed ZIP must come from `tools/package_release.ps1` after a
  successful x64 Release solution build. Do not zip `x64/Release` directly or
  reuse a historical package's file list.
- Supply the official, Microsoft-signed **x64 Visual C++ Redistributable** with
  `-VcRedistPath` (or `VP_VC_REDIST_X64`). The packager requires it and rejects
  installers older than any recorded build toolset or shipped PE linker family.
- Preserve `Directory.Build.targets` runtime records when collecting build
  outputs. Packaging verifies their binary hashes; missing or stale records
  require a rebuild. Do not fabricate records to get past a packaging failure.
- Review `packaging/release-manifest.json`'s `vcRuntime.minimumVersion` whenever
  compiler headers, Qt, or other binary dependencies change. PE linker versions
  only identify a family; the policy floor must cover dependency patch versions.
- Every portable ZIP must contain `START-HERE.txt`, `SETUP-RUNTIME.cmd`, and the complete
  `prerequisites/` directory, including the generated requirement JSON and official
  installer. Do not substitute loose Microsoft DLLs from System32 or the build tree.
- Portable ZIP instructions must tell users to run `SETUP-RUNTIME.cmd` before VP
  or Config after extracting/updating. A runtime with the right DLL name but an
  older version is insufficient; Config Apply/OK can crash in its mutex code.
- Setup executables embed and run prerequisite tools from their private temporary
  directory; do not install ZIP setup tools or development docs into the app folder.
- Run `tools/test_runtime_packaging.ps1` and the packaged setup's `-CheckOnly` mode.
  Release qualification also requires a clean Windows environment without Visual
  Studio: exercise missing/old and sufficient runtime cases, then edit, Apply,
  OK, and reopen Config to verify persistence. Record any untested installer/VM
  coverage explicitly; developer-machine success alone does not prove this path.
- See `docs/VP-0107_RELEASE_LAYOUT.md` for packaging commands and setup behavior.
