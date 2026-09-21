# VP-0070 Step 1 bounding-box diagnostic

Enable `subtitle_bbox_test: true` in `[vprenderer]`, then restart VP.
Default is false. VP Renderer only; no DirectShow/madVR implementation in this trial.
Select the physical 16:9 viewport. While enabled, presentation fits the complete
input raster with no crop, NLS warp or subtitle translation. It does not change
saved zoom, screen, or color settings. Disable the flag and restart to return.

A three-output-pixel bright green outline encloses the detected cue. It is
composited after source analysis; input pixels are never modified. No OCR,
models, glyph extraction, erasure or relocation. Every new frame is inspected.
Initial qualified detections are visible immediately. Two missed observations
may hold the outline (logged as `held=1`); that is not fresh detection evidence.
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
