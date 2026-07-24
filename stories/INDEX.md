# VideoProcessor plan index

This folder contains self-contained implementation stories and plans.  Filenames
are assigned monotonically and are never reused.

## Registry state

- Last assigned item: `VP-0012`
- Next story number: `VP-0013`
- Total indexed items: 12

## Items

| ID | Status | Title |
| --- | --- | --- |
| VP-0001 | Planned | NLS one-shot selection and queue safety |
| VP-0002 | Planned | Preserve armed NLS state through conditional aspect fallback |
| VP-0003 | Planned | Generic active rectangle support for NLS |
| VP-0004 | Planned | Reliable limited-range output investigation and implementation plan |
| VP-0005 | Planned | Alpha renderer timing observability and queue health |
| VP-0006 | Planned | Alpha renderer optional scene-safe queue-drop correction |
| VP-0007 | Planned | Alpha renderer controlled repeat correction |
| VP-0008 | Planned | Alpha renderer presentation-pacing assessment and refinement |
| VP-0009 | Planned | Alpha renderer DeckLink R210/R12B format parity |
| VP-0010 | Planned | Strict bar-only OCR subtitle replacement |
| VP-0011 | Blocked | Alpha renderer color-managed 3D LUT support |
| VP-0012 | Planned | Alpha renderer LUT pipeline contract spike |

## Story-state workflow

Every story has one current state. Keep the state in both the story's `## Status`
section and the table above so the index remains useful without opening every
file.

Use only these states:

| State | Meaning |
| --- | --- |
| Draft | Being written; requirements are not yet ready for implementation. |
| Planned | Ready to implement, but no implementation work has started. |
| In progress | Implementation is actively underway on a branch. Include the branch name and a short progress note in the story. |
| Blocked | Cannot proceed due to a concrete external dependency, decision, or reproducible issue. Record the blocker and next required action. |
| Validating | Implementation is complete enough for build/test or user validation. Record the commit and validation still required. |
| Complete | Accepted: implementation, appropriate validation, documentation/configuration updates, and merge or release decision are all complete. Include the final commit/release reference. |
| Deferred | Intentionally postponed. Record why and the condition that would make it relevant again. |
| Superseded | Replaced by another story. Link the replacement ID; do not reuse this story's number. |

## Updating the backlog

1. When adding an item, allocate the next number, create
   `VP-NNNN_<short-title>.md`, then update the Registry state values and table in
   the same commit.
2. Before changing a story from `Draft` or `Blocked` to `Planned` or `In
   progress`, perform and record a readiness review. Verify that:
   - the configuration section/key model matches the current code;
   - required API behavior, pipeline order, and resource lifetime are known or
     are covered by a completed technical spike;
   - upstream/dependent stories and platform constraints have an explicit
     completion boundary;
   - the validation method can prove the feature's important correctness
     claims; and
   - implementation will start from a clean, correctly based worktree.
   If an unknown could invalidate the feature design (for example, color
   pipeline placement, security boundary, concurrency model, or OS/API
   behavior), create a bounded spike story first rather than beginning product
   implementation on an assumption.
3. A spike story must state the unknown, non-production scope, reproducible
   evidence required, decision it will produce, and the story/stories it
   unblocks. Mark a dependent story `Blocked` and link the spike until the
   result is recorded.
4. When implementation starts, change the story and index to `In progress` and
   record the branch. Do not mark a story Complete merely because code was
   written.
5. When testing begins, use `Validating`; record build/test evidence and the
   specific user validation still needed.
6. Mark a story `Complete` only after it is accepted and its implementation has
   been merged or deliberately released. Add the final commit, PR, or release
   reference to the story's Status section.
7. Update this index in the same commit whenever a story's state, title, or
   completion reference changes. Story numbers remain permanent.
