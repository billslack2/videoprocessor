# VideoProcessor repository guidance

## Source and GitHub

- This is the authoritative source checkout:
  `C:\Users\bslac\vp\videoprocessor - VS2026`.
- `billslack2/videoprocessor` is the only GitHub repository of record. This is
  a permanent independent fork; never create, retarget, or merge pull requests
  against `enchywastaken/videoprocessor`.
- Before branching, creating a PR, or merging, discover the current GitHub
  default branch with:
  `gh repo view billslack2/videoprocessor --json defaultBranchRef --jq .defaultBranchRef.name`
  Use that result as the base unless the user explicitly requests another one.
- Verify the active checkout, branch, remote, and worktree status before Git
  writes. Feature branches should start from the current remote default branch.

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
