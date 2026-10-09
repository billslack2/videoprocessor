# VP-0070: Phantom Menace recording regression

This detection-only regression uses the user's actual recording, identified by
SHA-256 in `tools/subtitle_phantom_manifest.json`. The media stays local; it is
not distributed with the repository. The manifest and replay tool are checked
in so the same recording can be tested again without replacing its subtitles
with synthetic text.

The recording captures a 23.976 fps input on a 60 Hz desktop. The movie raster
is 1040x585 before the window is maximized and 2000x1125 afterward. Those pixels
are not the original 3840x2160 input. Tests crop the manually reviewed movie
raster without resizing, erasing old green boxes, inpainting, or applying OCR.
Player controls, old diagnostic overlays, desktop occlusion and window changes
are labelled separately. Maximizing the window is not a source aspect-ratio cut.

Manual annotations identify complete line envelopes, blank intervals, and a
set of complete cues with independently reviewed onset/end frames.
Annotations were made before comparing the new detector and stayed frozen.
Those cues subsequently informed debugging: they are regression evidence, not
an unseen validation set, despite the historical `heldout` IDs. Unknown intervals
still flow through the replay but cannot supply acceptance evidence.

## Run the actual pipeline

After building the x64 Release solution, build
`tools/subtitle_clip_probe.vcxproj` with the same configuration and platform.
The probe consumes decoded BGRA frames and runs the production bar evidence,
subtitle detector, queue confirmation and presentation tracker. It does not
read annotation coordinates. An explicit `--diagnostic-bars` option exists
only to isolate spatial failures; it cannot establish acceptance.

```powershell
python tools/test_subtitle_recording.py `
  --video 'C:\Users\bslac\Videos\2026-10-02 21-25-45.mp4' `
  --probe x64/Release/SubtitleClipProbe.exe `
  --output artifacts/phantom-replay --lookahead 3
```

Three future frames match the four-frame queue visible in this recording.
The default 24000/1001 sampling reduces repeated desktop frames, but cannot
prove the original input frame identities. An every-captured-frame replay is
useful for appearance/disappearance boundaries; it must not be presented as
independent source-frame confirmation or as original-4K performance evidence.

The harness records media, manifest, executable and decoded-frame hashes, the
crop and sampling policy, and both measured and displayed boxes. Assess every
line's containment, excess margins, first complete box, later growth, coordinate
changes throughout a cue, and boxes on negative frames. Any-box detection alone
is insufficient. Keep altered/derived images, supplied-bar diagnostics and
contaminated frames separate from the clean actual-pixel acceptance results.

## Diagnosed failure and scope

The previous trial borrowed the native crop analyzer's strict bar authority.
In the recorded case, its connected-picture veto treated subtitle strokes
crossing the boundary as picture content. The diagnostic therefore frequently
never ran the text detector. Supplied-bar replay is useful evidence of this
distinction, but not a fix or an acceptance test.

The diagnostic now proves horizontal black bars independently from current
pixels. Distributed columns establish flat dark bands and a locally sharp
picture boundary; broad picture intrusions and gradual dark gradients are
rejected. Each edge is independent. Crop authority is unchanged. A one-source-row
measurement fringe is tolerated only between freshly proven bars of the same
presence; losing a bar or moving it two rows breaks cue confirmation.

Reduced-resolution sampling also had a separate phase-dependent failure:
the regular stride could skip the only glyph row entering a bar. Sampling the
last top-bar row and first bottom-bar row explicitly fixes that loss while
retaining the regular grid elsewhere. Unit coverage pairs every UHD sampling
phase with an otherwise identical picture-only negative.

Letter-scale filtering limits picture fragments. Completed groups sharing a
baseline are reconciled before signatures are formed, so disconnected sampled
strokes cannot turn one text row into two competing lines. Small punctuation
can join established letters within a fixed distance from their original
extent; punctuation cannot build an unbounded chain. Temporal signatures use
only components assigned to the selected lines, not unrelated bright pixels
inside the rectangle. Conservative padding encloses antialias fringes and is
never used as proof that text enters a bar.

Detached question-mark and exclamation-mark dots are joined to their nearby
upper stroke before baseline grouping, with at most one dot per original
stroke. Companion lines must also have compatible horizontal centers. Bar ink
must belong to a substantial letter stroke, but a single sampled crossing
pixel is sufficient; unrelated tiny bar noise is not.

Picture-side components need a core near the text brightness learned from the
bar and close to its learned color. Lower-threshold antialias fringes remain
in accepted components under the looser mask color tolerance. If the
coarse grid finds only a weak sample, a bounded native neighborhood around its
brightest sample checks for an intervening thin stroke before rejection. This
avoids equating gray picture highlights with white text without scanning the
whole picture at native resolution.

A terminal question-mark dot can also fall wholly between grid columns. An
isolated short head next to an established line may attach only after a narrow
native-pixel search finds a matching dot below it near that line's baseline.
All 16 UHD horizontal/vertical sampling phases have paired dot-present and
dot-absent tests. Dot recovery does not manufacture bar authority.

## Recorded regression result

The previous native pipeline displayed none of the 807 clean annotated positive
samples because its crop-derived bar authority rejected those crossings. The
updated pipeline passes all 807 at the default 24000/1001 sample cadence and
three future observations: 19 cues each retain exactly one complete box, with
both lines where present, and none of the 234 clean negative samples has a box.
All 1,787 sampled movie-raster frames flow through the pipeline, including
unannotated and separately labelled contaminated intervals. Only the annotated
clean intervals grant acceptance.

This is a regression result on a recording used during development, not a claim
of universal subtitle recognition. The native replay separately records every
captured frame, queue-depth variants, strict edge containment and onset evidence.
Build-pinned reports and runnable-copy provenance belong in the VP-0070 tracker
validation record. CPU figures from these cropped desktop rasters are not
original-4K renderer or capture-card performance measurements.

This work remains Step 1. It draws a green rectangle and does not authorize
cropping, source erasure, glyph extraction or subtitle relocation. VP-0070 stays
Blocked until its complete-bound and consistency requirements are demonstrated
on representative live inputs.
