# VP-0201 local deployment â€” 2026-10-22

User requested deployment of the startup framing and prepared-subtitle Scope fixes.

Clean source [05e59c91](https://github.com/billslack2/videoprocessor/commit/05e59c91e40bcde9f75167506044b7671aca8503) is pushed on `codex/deploy-startup-subtitle-20261022`. The worktree started at freshly fetched beta `3746d59e`; it combines PR #188 (`6f12ff91`) with exactly the previously installed source `620b7d95`, preserving the installed clock, subtitle handoff, backing and surrounding-picture fixes. Its diff from the prior installed source consists only of #188's seven changed files. PR #188 remains draft/unmerged; no beta or release was published.

Full x64 Release solution build passed (73 warnings, zero errors). Host and both renderer variants embed clean commit `05e59c91` (`VERSION_DIRTY=false`). All **1,563 targeted native tests passed**, zero failed/skipped, across the original 27 policy classes plus clock, handoff, backing, reset-policy and config-file checks. Results and selected classes are retained beside this record. Build log: `C:\Users\bslac\Documents\Codex\2026-10-22\befo\work\vp-deploy-build.log`.

Deployed and SHA-256 verified:

- `C:\Videoprocessor\vp\VideoProcessor.exe`
- `C:\Videoprocessor\vp\vprenderer\VideoProcessorVPRenderer.dll`
- `C:\Videoprocessor\vp\vprenderer-beta\VideoProcessorVPRendererBeta.dll`

Backup: `C:\Videoprocessor\vp\backup-before-vp0201-05e59c91-20261022-183530`. All three backup hashes match their old installed files; all three deployed hashes match the same Release build and its build-time runtime records. Exact hashes and paths are in `deployment-receipt.json`. The guarded deployment script stages all files, replaces them with rollback on failure, and rejects unexpected source or installed-binary changes.

Configuration edits: none. Hashes of every root `.cfg`, `.ini` and `.state` file remained unchanged. The player was already stopped and was left stopped; the configuration editor was not closed. No live playback validation was claimed. Story remains Review for independent review and startup/subtitle playback acceptance.

Deployment retry note: the first attempt passed a null backup name to the Windows file-replacement API through PowerShell, which produced an invalid-path error. Rollback restored all three original runtime hashes, verified before retry. The corrected call uses explicit atomic backup paths. The earlier verified backup remains at `C:\Videoprocessor\vp\backup-before-vp0201-05e59c91-20261022-183437`; the successful deployment backup is the receipt path above. No source changes or test failures were involved.
