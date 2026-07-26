# VideoProcessor plan index

This folder contains self-contained implementation stories and plans.  Filenames
are assigned monotonically and are never reused.

## Registry state

- Last assigned item: `VP-0029`
- Next story number: `VP-0030`
- Total indexed items: 29

## Story locations

The directory is the canonical state. Every story is in exactly one of these
folders, and its `## Status` heading must use the matching state name.

`INDEX.md` is the sole authoritative workflow instruction for this tracker.
The state-folder `README.md` files are intentionally brief navigation labels;
they must not contain requirements that are absent from this file.

| Folder / state | Meaning |
| --- | --- |
| `draft` / Draft | Backlog, discovery, or a known dependency remains. Record dependencies, blockers, and next action in the story; do not create additional state names. |
| `in-progress` / In Progress | Codex is actively implementing the story on a recorded branch/worktree. Include current progress, commit(s), and any blocker under `## Status`. |
| `review` / Review | Implementation is ready for code review, build/test review, or requested user validation. Record evidence, remaining validation, and the proposed merge/release decision. |
| `done` / Done | Accepted and complete. Record the final commit, merge/release reference, and validation result. |
| `will-not-do` / Will Not Do | Intentionally retained but not implemented as separate work. Record the decision, reason, date, and any superseding story/commit. Do not delete or reuse its ID. |

## Items

| ID | State | Title |
| --- | --- | --- |
| VP-0001 | Done | NLS one-shot selection and queue safety |
| VP-0002 | Done | Preserve armed NLS state through conditional aspect fallback |
| VP-0003 | Done | Generic active rectangle support for NLS |
| VP-0004 | Done | Reliable limited-range output investigation and implementation plan |
| VP-0005 | Will Not Do | Alpha renderer timing observability and queue health |
| VP-0006 | Will Not Do | Alpha renderer optional scene-safe queue-drop correction |
| VP-0007 | Will Not Do | Alpha renderer controlled repeat correction |
| VP-0008 | Will Not Do | Alpha renderer presentation-pacing assessment and refinement |
| VP-0009 | Draft | Alpha renderer DeckLink R210/R12B format parity |
| VP-0010 | In Progress | Strict opaque-panel OCR subtitle replacement |
| VP-0011 | Review | Alpha renderer color-managed 3D LUT support |
| VP-0012 | Review | Alpha renderer LUT pipeline contract spike |
| VP-0013 | Done | DirectShow queue/reset alignment and no-drop review |
| VP-0014 | Will Not Do | Alpha renderer SDR BT.2020 source and target support (duplicate of VP-0019; will not do separately) |
| VP-0015 | Draft | Alpha renderer shader compatibility with the existing shader catalog |
| VP-0016 | Will Not Do | Make scene detection available for the alpha renderer |
| VP-0017 | Will Not Do | Explain variable alpha-renderer queue depth |
| VP-0018 | Done | Re-select the content refresh rate when switching to alpha |
| VP-0019 | Done | SDR BT.2020 display profiles, F5/F6 hotkeys, and output signaling |
| VP-0020 | Draft | v210 arbitrary-width and DCI P010/P210 support |
| VP-0021 | Draft | Renderer format negotiation parity and truthful capability reporting |
| VP-0022 | Draft | DeckLink encoded-format boundary and diagnostics |
| VP-0023 | Draft | P010 range metadata contract and conversion regression tests |
| VP-0024 | Draft | Alpha source-to-display timing and queue telemetry |
| VP-0025 | Draft | Renderer-neutral scene detection and Alpha integration |
| VP-0026 | In Progress | Alpha low-latency elastic queue |
| VP-0027 | Draft | Alpha display-verified scene-safe cadence correction |
| VP-0028 | In Progress | Renderer configuration profile, hotkey, and event-action unification |
| VP-0029 | Draft | Alpha two-pass display LUT and final-dither pipeline spike |

## Codex story workflow

`origin/main` is the canonical source of truth for story state. Story status,
folder placement, the registry, and workflow instructions must always be
tracked from the current story-tracker `main` branch so a later session can
discover and update them reliably. Secondary story checkouts are disposable
working copies, not independent authorities.

Before reading, creating, moving, or committing a story, Codex must fetch
`origin/main` and verify the working copy is based on the current remote main.
Use a clean main-based checkout for the change. If an existing checkout is
dirty or based on an older main, do not merge, rebase, reset, or overwrite its
uncommitted work; create a fresh checkout from `origin/main` instead. A story
change is not complete until it is committed and pushed to the canonical
`main` branch, unless the user explicitly requests another review branch.

Codex owns moving stories between folders in this session. Move the file,
update its `## Status` section, and update the table above in the **same
commit**. Never copy a story into a second state folder; `git mv` is preferred
so history follows the file.

## Implementation branch gate

Story documents and story-state changes always commit directly to the tracker
`main` branch. Implementation work for a story in the VP source repository
does **not** assume that `main` is the integration base. Before starting that
implementation work, Codex must manually query the current default branch of
`origin` for `billslack2/videoprocessor` (for example with `git remote show
origin`) and report it to the developer.

Codex must then ask the developer to confirm that discovered default branch as
the implementation base or to name another branch. Do not create a feature
branch, worktree, PR, or implementation commit until that confirmation is
received. Record the confirmed base branch and the implementation branch/worktree
in the story when it moves to `in-progress`. Re-run this manual discovery and
confirmation gate whenever a new story implementation starts; the origin
default branch may change.

1. Create new stories in `stories/draft/` using the next permanent ID, then
   update the Registry state and table in this file.
2. Before moving a Draft to `in-progress`, record a readiness review in the
   story. Verify that the configuration model matches current code; required API
   behavior, pipeline order, and resource lifetime are known; dependencies and
   platform constraints have an explicit completion boundary; validation can
   prove important correctness claims; and work will use a clean, correctly
   based worktree.
3. If an unknown could invalidate the design (for example color-pipeline
   placement, security boundary, concurrency model, or OS/API behavior), create
   a bounded Draft spike first. A spike must state its unknown, non-production
   scope, reproducible evidence, decision, and the stories it unblocks. Keep
   the dependent story in Draft and link the spike.
4. When implementation begins, move the story to `in-progress/`, set its
   status to `In Progress`, and record the branch/worktree and first progress
   note in the same tracker change. Subsequent meaningful progress, blockers,
   branch changes, and validation results must be recorded in the story while
   work continues. Do not leave an actively implemented story in `Draft`, and
   do not move it merely because investigation or discussion started.
5. When implementation is ready for independent assessment, move it to
   `review/`, set its status to `Review`, and record build/test evidence plus
   completed work, build/test evidence, and exactly what reviewer or user
   validation remains.
6. Move a story to `done/` only after acceptance, appropriate validation, and
   merge or deliberate release. Set its status to `Done` and record the final
   commit/PR/release reference.
7. A story may move backward—for example Review to In Progress after a failed
   test, or In Progress to Draft when a new prerequisite is found. Record why
   in `## Status`; do not invent additional state names.

Terminal disposition: move a story to `will-not-do/` when it is intentionally
declined, duplicated, superseded, or otherwise determined not to be separate
work. Set its status to `Will Not Do` and record the decision, reason, date,
and any replacement story/commit. This is a tracked terminal state, not
deletion.
