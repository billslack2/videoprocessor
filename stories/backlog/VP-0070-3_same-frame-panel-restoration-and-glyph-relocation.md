# VP-0070-3: Same-frame panel restoration and glyph relocation

## Status

Backlog. Depends on VP-0070-2.

## Parent

[VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

For a valid panel-bound cue, restore the source glyph area to its learned panel
color and composite the captured visual glyph mask onto a locked destination
panel in the same Alpha or DirectShow/madVR frame. The feature is opt-in and
fails safe to unchanged input when panel evidence is not sufficient.

On a first candidate frame, a synchronous, allocation-free
`safeToSuppress` prefilter may inpaint only a conservatively expanded glyph
mask. Relocation waits for the stable CueSet. If that gate does not pass, VP
shows the original frame; without buffering, zero original-caption flash and
zero false suppression cannot both be guaranteed.

## Acceptance criteria

- A treated frame never displays both source and destination glyphs.
- Candidate-frame suppression has zero false suppression on the agreed
  title/menu/OSD negative corpus and leaves at most 1% source-glyph residual.
- Source restoration uses the learned panel color, not a hard-coded black.
- Destination panel/glyph geometry remains fixed for the cue.
- Disable, generation change, or invalid evidence returns to unchanged output
  without stale pixels or a renderer restart.
- Optional OCR/text evidence remains asynchronous; no blocking readback,
  presentation wait, allocation, or unbounded queue is introduced.
