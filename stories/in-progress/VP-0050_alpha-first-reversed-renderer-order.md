# VP-0050: Put Alpha first and reverse renderer order

## Status

In Progress. Implementation started on `codex/vp-0050-renderer-order` from
`origin/v1.1.014-beta` in a clean worktree. Ordering policy and focused native
test coverage are in progress.

## User story

As a VideoProcessor user, I want the built-in Alpha renderer to be the first
choice in the renderer list and all other renderers to appear in the reverse of
their current order, so VP's preferred renderer is prominent and the remaining
choices follow the requested presentation order.

## Current behavior

`CVideoProcessorDlg::RebuildRendererCombo()` currently discovers eligible
DirectShow renderers, adds Alpha when its optional renderer component is
available, filters entries whose names contain `DeckLink`, and then sorts the
combined list in its existing ascending order.

This allows Alpha to appear among the external renderers instead of always
appearing first.

## Required behavior

1. When Alpha is available, `VideoProcessor Renderer (Alpha)` is always the
   first item in the renderer combo box.
2. Every remaining eligible renderer appears in the exact reverse of the
   order produced by the current implementation after filtering.
3. Entries whose names contain `DeckLink`, case-insensitively, remain excluded.
4. If the optional Alpha component is unavailable, VP starts normally and
   lists the remaining renderers in the requested reversed order.
5. Renderer selection by configured renderer name remains authoritative. The
   ordering change must not silently select Alpha when another valid renderer
   is explicitly configured.
6. Existing renderer shortcuts continue to select the one-based position
   shown in the combo box. Their indices therefore follow the new visible
   ordering.
7. Renderer discovery, construction, switching, and teardown behavior are
   otherwise unchanged.

## Implementation guidance

Make the ordering policy explicit rather than depending on Alpha's display
name to sort favorably:

- partition Alpha from the filtered external renderer entries;
- sort or reverse the external entries using the same ordering contract that
  defines today's visible order;
- append Alpha first when available, followed by those external entries in
  reverse order; and
- retain the current configured-name matching after each item is inserted.

Keep the existing renderer-order diagnostic so the final sequence can be
confirmed from the debug log.

## Verification

Add a focused ordering test using a deterministic set of renderer entries:

- Alpha is first regardless of its display name.
- The remaining entries are the reverse of the previous visible order.
- `DeckLink` names are excluded before ordering.
- Alpha absence produces only the reversed external list.
- An explicitly configured external renderer remains selected.
- One-based renderer shortcut indices match the displayed order.

Manually verify the renderer combo and `Renderer order: render.N = ...` log
lines in an x64 Release build with Alpha available and with its optional
component absent.

## Acceptance criteria

- Alpha is renderer item 1 whenever it is available.
- All other visible renderers are in the reverse of their current order.
- Alpha's absence remains silent and does not prevent VP from starting.
- DeckLink filtering, configured renderer selection, and renderer switching
  continue to work.
- Automated ordering coverage and x64 Release validation pass.
