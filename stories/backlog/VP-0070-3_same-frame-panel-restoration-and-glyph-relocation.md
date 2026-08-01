# VP-0070-3: Same-frame panel restoration and glyph relocation

## Status

Backlog. Depends on VP-0070-2.

## Parent

[VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

For a valid panel-bound cue, restore the source panel to its learned solid
color and composite the captured visual glyph mask onto a locked destination
panel in the same Alpha frame. The feature is opt-in and fails safe to
unchanged input when panel evidence is not sufficient.

## Acceptance criteria

- A treated frame never displays both source and destination glyphs.
- Source restoration uses the learned panel color, not a hard-coded black.
- Destination panel/glyph geometry remains fixed for the cue.
- Disable, generation change, or invalid evidence returns to unchanged output
  without stale pixels or a renderer restart.
- No OCR, text recognition, neural inference, blocking readback, or unbounded
  queue is introduced.

