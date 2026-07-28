# VideoProcessor story tracker guidance

- Stories in this directory are the authoritative VP story records.
- Keep this tracker on its `main` branch and synchronize with `origin/main`
  before making story-state edits.
- When a story state, queue position, acceptance result, or completion status
  changes, commit that tracker change and push it to `origin/main` promptly.
- Story state is part of the work, not a retrospective label: as soon as
  implementation starts, move the story from `backlog/` to `in-progress/` and
  keep its status and progress evidence current as the work advances.
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
- VP source work is performed in
  `C:\Users\bslac\vp\videoprocessor - VS2026`; do not treat the source tree
  containing this tracker as the authoritative implementation checkout.
- For VideoProcessor testing and troubleshooting, inspect the debug log at
  `C:\logs\vp_debug.log`.
- The VP GitHub repository of record is `billslack2/videoprocessor`. Discover
  its current default branch before creating source branches or pull requests;
  do not assume the default branch is named `main`.
