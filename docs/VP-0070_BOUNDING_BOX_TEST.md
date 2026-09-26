# VP-0070 Step 1 bounding-box diagnostic

Enable `subtitle_bbox_test: true` in `[vprenderer]`, then restart VP.
Default is false. VP Renderer only; no DirectShow/madVR implementation in this trial.
Select the physical 16:9 viewport. While enabled, presentation fits the complete
input raster with no crop, NLS warp or subtitle translation. It does not change
saved zoom, screen, or color settings. Disable the flag and restart to return.

A three-output-pixel bright green outline encloses the detected cue. It is
composited after source analysis; input pixels are never modified. No OCR,
models, glyph extraction, erasure or relocation. Every new frame is inspected.
Initial qualified detections are visible immediately. An outline may hold for
at most two failed complete-cue observations only while a fresh accepted
bar anchor remains inside the previous cue (logged as `held=1`). Loss of bar
evidence clears it immediately, even if picture-side text remains.
The detector does not authorize any source treatment.

`SUBTITLE BBOX` logs record cue, source frame, line count, geometry, current
observation versus held state, geometry revisions, trusted bar availability,
CPU cost and detected/held totals. A revision can be a real new cue or a late
correction; compare recording and source frames. Early means the first eligible
frame *after trusted bar authority exists*, not the first frame after startup.

Play SDR/HDR captions wholly in bars and crossing the boundary, multi-line cues
with a wider upper line, short words, dark/bright scenes, subtitle changes,
menus, logos and clean frames. Record first subtitle frame to first complete box,
late expansions, misses, false boxes, stable-cue coordinate drift and cost.
The box must include every line from onset. Passing synthetic fixtures is not
live acceptance. Colored/stylized text and stationary background edges remain
important real-video cases to qualify before extraction work.

Eligibility requires pixels from the accepted glyph components of the anchor
line to lie in the trusted encoded bar. Bounding-box padding, rejected components
and unrelated bright pixels inside the rectangle never count as bar evidence.
This is a geometric eligibility gate, not proof that every accepted shape is text;
real-video false-positive validation remains required.

Detection samples at most 960x540, using an integer source stride. For 3840x2160
this is a 4-pixel stride on both axes (one sixteenth of the source pixel count).
Only bar/boundary strips are sampled; rendering stays full resolution and source
coordinates are restored for the outline. Logs report `analysis_step`. No OCR or
full-resolution subtitle scan is introduced by this diagnostic.
