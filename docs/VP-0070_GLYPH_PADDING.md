# VP-0070: glyph-relative, stable subtitle display padding

## Contract

The subtitle detector continues to own eligibility, glyph capture, and measured
opaque source-card cleanup. Display padding must not enlarge any of those masks.
The AR system continues to own the visible picture and genuine format changes.

Glyph mode measures the height of accepted glyph rows, without recognizing
characters. It uses a median of the available consecutive same-cue lookahead
measurements. Short detached punctuation rows remain in the complete glyph
bounds but do not set the font scale. Two- and three-line captions use a typical
individual line height, not the total block height. Joined scripts and unknown
characters do not require different OCR for this display calculation.

At acquisition, the display guard freezes that height, the calculated margins,
and the complete accepted glyph envelope. It reuses the existing neighboring-ink
padding guard once, using the acquisition window. Ordinary current-frame grouping
noise, weak detection and held frames do not change these display metrics.
Only a cue-tracker-confirmed text expansion may grow the glyph envelope, while
keeping the acquired padding. A new cue, source/viewport/context discontinuity,
or explicit padding-policy change starts a new acquisition. An invalid or empty
preview clears it. Missing line measurements use the configured fixed-pixel
padding for the entire cue; later measurements cannot cause a mid-cue jump.

The measured source card remains the minimum horizontal display width. The prior
same-cue width guard permits safety expansion and prevents shrinkage. Fresh source
card measurements still control cleanup/capture independently of that display
history. Near-bar subtitles already inside the picture are not pulled toward the
bar. Placement still fits inside the AR-owned picture; genuine picture changes
are intentionally not frozen by this caption policy.

## Configuration

Place these settings in `[vprenderer]` (also accepted in display profiles):

```ini
subtitle_box_padding_mode: glyph
subtitle_box_padding_sides_percent: 75
subtitle_box_padding_top_percent: 35
subtitle_box_padding_bottom_percent: 25
```

These are the runtime defaults. Percentages are integers from 0 through 200.
Each margin rounds upward to whole source pixels and is capped at 500 source
pixels. For an 80-pixel glyph row, the defaults request 60 pixels on each side,
28 above, and 20 below. A two-line subtitle with the same font requests the same
margins. The existing neighboring-ink guard may reduce a margin once at acquisition.

`subtitle_box_padding_mode: fixed` restores the existing
`subtitle_box_padding_sides`, `subtitle_box_padding_top`, and
`subtitle_box_padding_bottom` values. Those values are also the missing-metric
fallback; they are not silently reinterpreted as percentages. The independent
six-pixel background side extension and source-card minimum coverage still apply.

No configuration UI is added. `SUBTITLE PADDING` records cue, mode, measured line
height, frozen margins and display bounds at acquisition or a confirmed revision.

## Cost and validation

The estimator examines at most three accepted row rectangles per already-buffered
observation, at acquisition only. It adds no image readback, pixel scan, GPU pass,
OCR call, sampling latency, or new worker. Existing pixel-based padding protection
is reused without running it again on unchanged cues.

Regression tests cover several font sizes; one, two and three rows; detached marks;
per-frame height/width noise; held frames; confirmed expansion; cue replacement;
lookahead continuity and unrelated future text; fixed fallback; configuration
changes; discontinuities; current-only capture/cleanup; and percentage bounds.
The full suite also checks shader composition and aspect-ratio integration.

Synthetic tests do not qualify all real fonts. The latest recording was useful
for previous near-bar placement diagnostics; it cannot independently reveal raw
source masks after processing. Playback should confirm visual proportions across
fonts while the log verifies frozen metrics for each accepted cue.
