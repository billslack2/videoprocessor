# VideoProcessor story tracker guidance

- Stories in this directory are the authoritative VP story records.
- Keep this tracker on its `main` branch and synchronize with `origin/main`
  before making story-state edits.
- When a story state, queue position, acceptance result, or completion status
  changes, commit that tracker change and push it to `origin/main` promptly.
- Story state is part of the work, not a retrospective label: as soon as
  implementation starts, move the story from `backlog/` to `in-progress/` and
  keep its status and progress evidence current as the work advances.
- VP source work is performed in
  `C:\Users\bslac\vp\videoprocessor - VS2026`; do not treat the source tree
  containing this tracker as the authoritative implementation checkout.
- For VideoProcessor testing and troubleshooting, inspect the debug log at
  `C:\logs\vp_debug.log`.
- The VP GitHub repository of record is `billslack2/videoprocessor`. Discover
  its current default branch before creating source branches or pull requests;
  do not assume the default branch is named `main`.
