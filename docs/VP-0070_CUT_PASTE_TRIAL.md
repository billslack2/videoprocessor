# VP-0070 subtitle consistency experiment

Story status remains **Blocked**. This is an isolated diagnostic experiment built
on the latest beta integration tip plus prior subtitle work. It does not
establish reliable recognition of arbitrary content.

## Detection and continuity

At least part of the actual text must be in a currently proved black bar. Padding
alone cannot qualify picture-only text. Analysis uses a grid no larger than
960x540, including explicitly sampled boundary rows. No OCR is used.

The existing queue supplies lookahead without adding buffering. Current pixels
must support a cue; future text cannot appear early. Once acquired, the cue owns
a fixed glyph mask and stable bounds. Every distinct source frame validates the
mask against current pixels. Comparisons include glyph interiors and holes,
while excluding unrelated scenery between disconnected glyph components.
Changed or missing glyphs, lost bar proof, source changes and discontinuities
invalidate reuse. Repeated presentation of the same source frame reuses analysis.

An exact current-pixel match may survive segmentation failure through the final
visible frame without requiring the same sentence to return in lookahead. This
is not a timed hold of absent text. New ink is also inspected outside acquired
lines so cached identity cannot indefinitely hide a new word or second line.
Confirmed extensions need current evidence and stable future evidence.

The October 3 rendered-output recording exposed repeated 50–53 source-pixel
position reversions while the same sentence remained visible. The tracker had
been allowing unstable line grouping to overrule the acquired glyph mask. An
established cue now keeps its accepted geometry when the full learned glyph
mask still matches current pixels in the same continuous frame and bar context,
even if the detector's reported grouping shifts. A total detector miss is held
only when no new glyph-like extension appears; new second lines still require
current and future confirmation. Changed glyphs, missing learned strokes,
context/bar changes, and discontinuities still release the cue.

The AlternatingDetectorBoundsCannotFlashAnUnchangedSubtitleByFiftyPixels test
reproduces the recorded failure shape: current glyph pixels stay fixed while
successive detector bounds drift by 10 pixels, defeating short lookahead.
It verifies stable source and destination geometry. This synthetic input
isolates the tracker regression; the new recording is already rendered output,
so it cannot be replayed as clean detector input or establish live end-to-end
success.

Geometry and glyph identity can be reused; background image pixels are never
cached. Each moved frame samples its own immutable source. Cleanup and drawing
are one GPU operation, with the destination taking precedence in overlap.

## Settings and modes

Set these integer source-pixel distances in `[vprenderer]` (or the active named
renderer profile). Accepted range is 0 through 500. Restart after editing.

```ini
subtitle_box_padding_sides: 60
subtitle_box_padding_top: 60
subtitle_box_padding_bottom: 20
subtitle_move_inset: 0
```

These defaults double the prior top/side buffer and double the lower buffer.
They affect both the green outline and moved panel, not raw eligibility or cue
identity. The larger margin contains detached leading marks and the visible
rounded subtitle backing. Generated blend/black/gray panels use an antialiased
rounded rectangle with radius derived from panel height and capped at 28 source
pixels. The destination backing meets the active-picture lower edge by default;
20 pixels of bottom padding remains inside it below the glyphs. Set a positive
inset only when an intentional gap is wanted. Keyed modes separate
the detected content rectangle from that panel: extra padding does not create
new glyph candidates or enlarge source cleanup. Rectangle mode copies the whole
padded patch. Bounds clip to the
source raster. Too-tall or non-upward placements leave the input unchanged.

`subtitle_bbox_test: true` displays the green box. Mutually exclusive
`subtitle_cut_paste_test: true` enables movement. Both display the complete
unwarped source raster for this experiment.

`subtitle_cut_paste_background` selects:

| Value | Result |
| --- | --- |
| `rectangle` | Copy the entire current-frame padded patch. Vacated source is black. |
| `transparent` | Approximate keyed text over destination; smooth the original detected text area from current-row samples beside its padded rectangle. |
| `blend` | Same approximate cleanup, with a half-strength linear-light destination backing for SDR/PQ. |
| `black` | Keyed text over an opaque black destination panel. Vacated detected content is black. |
| `dark_gray` | Keyed text over an opaque panel at 8% reference white (16.24 nits for PQ). Vacated detected content is black. |
| `generated_gray` | Keyed text over live destination picture with a rounded, 45%-opacity backing at 8% reference white. A deterministic picture extension fills vacated source content within the active picture. |

The keyed modes sample glyphs across the complete detected bounds, including text
that crosses from the active picture into a black bar. Black pixels from the bar
do not travel with the text: transparent mode shows the destination picture,
blend mode dims it, and the original bar remains black after cleanup. The black
and dark-gray modes intentionally place their solid panel behind the whole moved
subtitle. A GPU regression covers bright neutral glyph cores on both sides of
the boundary; the approximate key still does not guarantee colored glyphs, dark
outlines, or faint antialiasing.

The generic START-SUBTITLE-MOVE.cmd selects transparent mode. Rectangle mode
is available through the explicit START-SUBTITLE-MOVE-RECTANGLE.cmd shortcut
and intentionally copies the source patch, including bar-black pixels.

The key favors bright near-neutral pixels. It is **not** a semantic glyph matte:
colored glyphs, dark outlines and scene highlights are not fully handled.
Transparent/blurred cleanup cannot recover hidden picture detail. Side samples
may themselves contain scenery or text. HLG composition is scene-linear, not a
promise of half displayed luminance. BT.1886 uses a zero-black gamma approximation.

The shader performs SDR/PQ blending in decoded light, then returns to the input
transfer. This avoids treating PQ code values as brightness. The hook remains
before output color management. No full-resolution CPU pixel-copy loop is added.

`generated_gray` is a bounded visual prototype. The destination keeps real
movie pixels under the gray backing, including where the mapped source patch
would have contained black-bar pixels; only keyed glyphs move from that patch.
For the original text area that must be cleared inside the active picture,
the shader reflects clean rows above the detected bounds, softens them over
five horizontal samples, and feathers to clean side samples at the edges.
The bar outside the active picture stays black. This uses one deterministic
method over the whole filled area and the current frame, without random seeds
or random texture selection. It cannot reconstruct detail hidden by subtitles
or black bars, and moving scenery can make the generated source cleanup change
over a held cue. True cue-frozen inpainting would require a separate persistent
frame/texture cache and validation at scene cuts and cue revisions.

`SUBTITLE MODE` logs the effective mode and padding; `SUBTITLE BBOX` reports cue,
frame, current/held state, geometry, bar authority and costs. The generic MOVE
launcher selects transparent backing. `MOVE-RECTANGLE` explicitly selects raw
patch copy; each named launcher selects its own cfg.

## Verification and limits

The isolated test folder contains the current build receipt, binary hashes and
measured results. Native tests exercise onset/end, changed glyphs, missing lines,
new lines, scene movement, context changes and geometry. Offscreen GPU readback
checks immutable current pixels, overlap cleanup, disabled identity and expected
backing values across sRGB, gamma 1.8/2.0/2.2/2.4/2.6/2.8 and PQ.

`tools/test_subtitle_recording.py` replays the earlier original-pixel recording
through the production detector/tracker. Its annotated cues informed earlier
tuning: these are regressions, not unseen validation. Raw completeness/tightness
and negative gates remain separate from the requested larger presentation padding.

`tools/test_subtitle_recorded_output.py` observes the two newer recordings without
altering them. Its SHA-pinned manifest records historical box loss, drift and
movement reversions. Those videos contain already rendered boxes or relocated
text, so passing the observer is not a detector acceptance test and changes to
source code cannot repair their recorded pixels. A fresh playback/capture is
needed to confirm the new build fixes the same live scenes. HLG GPU behavior,
live 4K throughput and arbitrary subtitle styles remain unqualified.

`tools/test_subtitle_gap_diagnostics.py` separately exercises two manually
reviewed no-overlay gaps from the newer box recording. It pins original media
and probe hashes, uses unchanged crop pixels, and checks complete bounds plus
stable box/cue identity with lookahead 3 and 0. The longer slice contains only
three sampled observations; the other contains one. This is a useful targeted
regression, not continuous playback, clean source acquisition, onset proof or
GPU acceptance. The diagnostic manifest records that selection bias explicitly.
