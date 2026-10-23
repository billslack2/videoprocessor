# VP-0201 beta merge — 2026-10-23

User authorized merging [PR #188](https://github.com/billslack2/videoprocessor/pull/188). It is merged into `v1.4.02-beta` at [f7c7a820](https://github.com/billslack2/videoprocessor/commit/f7c7a8204f7a2cace68b3b40774406680af6c0d8).

The initial pinned merge gate correctly stopped when beta advanced from `3746d59e` to `fcc33fc9` while preparation was running. A new clean worktree under `E:\codex\videoprocessor\ar-startup-subtitle-merge-20261023` started at the fetched current beta, and original feature `6f12ff91` merged cleanly at tested head `5d3db5f230b05dab3e0b8e1a7b59218ff81c9a80`. Its runtime and native-test source matches the previously deployed combination `05e59c91`; the remaining differences are the current beta's Config subtitle-fill picker and its tests. Those changes retain saved legacy Temporal values while removing the selectable option.

Exact integration validation: full x64 Release solution build passed (73 warnings, zero errors), **1,563 / 1,563 targeted native tests passed**, and both Config checks passed: subtitle surrounding fill round trip; subtitle retired Temporal fill preserves saved profiles. Saved results and selected class names are included here. This is targeted coverage, not a full native/Config suite run.

The named PR branch was fast-forwarded to the tested integration, prepared using the VP merge helper, marked ready and merged using the same pinned source and base. GitHub reported no status checks; no checks were bypassed. The fetched merge commit has first parent `fcc33fc9` and exactly the tested tree `6e1b76cb3cf94e079ae8965b5cf8dcb41967c4cc`. Source and test identities are recorded in `validation.json`.

No deployment, configuration edit, player restart or release packaging was performed for this merge request. The prior local deployment record remains unchanged. VP-0201 remains Review for live startup/subtitle playback acceptance, which has not been reported.
