# VideoProcessor plan index

This folder contains self-contained implementation stories and plans.  Filenames
are assigned monotonically and are never reused.

## Registry state

- Last assigned item: `VP-0014`
- Next story number: `VP-0015`
- Total indexed items: 14

## Story locations

The directory is the canonical state. Every story is in exactly one of these
folders, and its `## Status` heading must use the matching state name.

| Folder / state | Meaning |
| --- | --- |
| `draft` / Draft | Backlog, discovery, or a known dependency remains. Record dependencies, blockers, and next action in the story; do not create additional state names. |
| `in-progress` / In Progress | Codex is actively implementing the story on a recorded branch/worktree. Include current progress, commit(s), and any blocker under `## Status`. |
| `review` / Review | Implementation is ready for code review, build/test review, or requested user validation. Record evidence, remaining validation, and the proposed merge/release decision. |
| `done` / Done | Accepted and complete. Record the final commit, merge/release reference, and validation result. |

## Items

| ID | State | Title |
| --- | --- | --- |
| VP-0001 | In Progress | NLS one-shot selection and queue safety |
| VP-0002 | In Progress | Preserve armed NLS state through conditional aspect fallback |
| VP-0003 | In Progress | Generic active rectangle support for NLS |
| VP-0004 | Draft | Reliable limited-range output investigation and implementation plan |
| VP-0005 | Draft | Alpha renderer timing observability and queue health |
| VP-0006 | Draft | Alpha renderer optional scene-safe queue-drop correction |
| VP-0007 | Draft | Alpha renderer controlled repeat correction |
| VP-0008 | Draft | Alpha renderer presentation-pacing assessment and refinement |
| VP-0009 | Draft | Alpha renderer DeckLink R210/R12B format parity |
| VP-0010 | Draft | Strict bar-only OCR subtitle replacement |
| VP-0011 | Draft | Alpha renderer color-managed 3D LUT support |
| VP-0012 | Draft | Alpha renderer LUT pipeline contract spike |
| VP-0013 | In Progress | DirectShow queue/reset alignment and no-drop review |
| VP-0014 | Draft | Alpha renderer SDR BT.2020 source and target support |

## Codex story workflow

Codex owns moving stories between folders in this session. Move the file,
update its `## Status` section, and update the table above in the **same
commit**. Never copy a story into a second state folder; `git mv` is preferred
so history follows the file.

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
   note. Do not move it merely because investigation or discussion started.
5. When implementation is ready for independent assessment, move it to
   `review/`, set its status to `Review`, and record build/test evidence plus
   exactly what reviewer or user validation remains.
6. Move a story to `done/` only after acceptance, appropriate validation, and
   merge or deliberate release. Set its status to `Done` and record the final
   commit/PR/release reference.
7. A story may move backward—for example Review to In Progress after a failed
   test, or In Progress to Draft when a new prerequisite is found. Record why
   in `## Status`; do not invent additional state names.
