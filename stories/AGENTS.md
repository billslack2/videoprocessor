# VideoProcessor story tracker guidance

- Stories in this directory are the authoritative VP story records.
- `C:\Users\bslac\vp\story-tracker\stories` is the dedicated authoritative
  tracker checkout. Keep it on `main` and synchronized with `origin/main`: at
  the beginning of tracker work fetch and fast-forward it; before ending the
  work commit, rebase, and push every intentional tracker change so `main` is
  clean and matches `origin/main`. Never leave story-state edits only in this
  checkout's working tree or use it as an isolated rewrite/worktree baseline.
- For any story lookup, review, planning, or status decision, fetch and read
  the record from `origin/main` first. Do not rely on the current worktree,
  local `main`, a source checkout, or an unverified commit lookup as the
  authoritative story state.
- Keep this tracker on its `main` branch and synchronize with `origin/main`
  before making story-state edits.
- When a story state, queue position, acceptance result, or completion status
  changes, commit that tracker change and push it to `origin/main` promptly.
- Story state is part of the work, not a retrospective label: as soon as
  implementation starts, move the story from `backlog/` to `in-progress/` and
  keep its status and progress evidence current as the work advances.
- Treat `C:\Users\bslac\vp\story-tracker\stories\in-progress` as a live
  queue. For every state transition, atomically `git mv` the record, update
  its exact `## Status`, and update `INDEX.md`; before committing and after
  rebasing, verify that every state-folder record and index row agree. Closed,
  review, or backlog stories must never remain in `in-progress/`.
- Before assigning a new story ID, audit every canonical state folder for
  `VP-####` story filenames and compare the discovered IDs with `INDEX.md`.
  Do not trust the registry's `Next story number` by itself. The new ID must be
  greater than the highest ID found in either the canonical story files or the
  index registry/table, even when the index is stale.
- Detect and report duplicate IDs, a story file missing from the index, an
  index item without one canonical story file, and any registry/count mismatch.
  Do not silently renumber, overwrite, or move an existing story to repair a
  conflict. Record or request a deliberate tracker-repair decision, while still
  using an ID higher than every known ID for genuinely new work.
- Re-run the ID audit after fetching `origin/main` and again if a push is
  rejected because remote story state advanced. This prevents concurrent
  sessions from reusing a newly assigned ID.
- A root story may be decomposed into a small, ordered set of independently
  testable child tasks using the `VP-####-N` form (for example, `VP-0066-1`).
  Child tasks are stable tracker items, not temporary checklist bullets. The
  detailed decomposition, ID-audit rules, and parent/child completion rules
  are authoritative in `INDEX.md`.
- VP source work is performed in
  `C:\Users\bslac\vp\videoprocessor - VS2026`; do not treat the source tree
  containing this tracker as the authoritative implementation checkout.
- For VideoProcessor testing and troubleshooting, inspect the current deployed
  debug log at `C:\Videoprocessor\vp\logs\vp_debug.log`. For an incident that
  occurred before a restart or log rotation, inspect the numbered rotated logs
  in that same directory (`vp_debug.log.0` through the configured retention
  limit) as well; do not rely only on the newly created current log.
- Every VP binary deployment must treat `VideoProcessor.exe` and
  `vprenderer\VideoProcessorVPRenderer.dll` as an inseparable versioned pair.
  Back up and replace both from the same successfully completed x64 Release
  build/commit, then verify both deployed hashes against the build artifacts.
- The VP GitHub repository of record is `billslack2/videoprocessor`. Discover
  the current remote beta integration branch and its exact tip before source
  work; do not hard-code a historical beta name or use `main` as the source
  integration base. Use a different branch only when the user explicitly
  requests one. Before creating source branches or pull requests, also verify
  the current GitHub default branch.
- Windows environment-variable names are case-insensitive: `Path` and `PATH`
  are one environment entry, never two. Do not declare, pass, or document
  them as separate variables, and do not use `$PATH` as a PowerShell variable.
  Use task-specific names such as `$vpPathValue` for local path strings. When
  changing the process search path, read and update only `$env:Path` (preserve
  its existing value and append/prepend the required directory); use a
  task-specific environment variable such as `$env:VP_*` for all other
  settings. Before running a command, inspect the effective `$env:Path` if
  path resolution matters.
