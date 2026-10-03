# VP-0070 Step 1 bounding-box diagnostic

Enable `subtitle_bbox_test: true` in `[vprenderer]`, then restart VP.
The flag also works in a named Rendering profile such as
`[vprenderer.profile_1]`; Config may rename the root when saving profiles.
Default is false. VP Renderer only; no DirectShow/madVR implementation in this trial.
Select the physical 16:9 viewport. While enabled, presentation fits the complete
input raster with no crop, NLS warp or subtitle translation. It does not change
saved zoom, screen, or color settings. Disable the flag and restart to return.

A three-output-pixel bright green outline encloses the detected cue. It is
composited after source analysis; input pixels are never modified. No OCR,
models, glyph extraction, erasure or relocation. Every new frame is inspected.
The renderer uses the existing queue's available look-ahead without waiting for
extra frames. New cues need a matching following observation when look-ahead is
available. The current frame must already contain a qualifying text line; a
future subtitle cannot start a box on an earlier blank frame. Matching later
observations can complete a multi-line box before its first display.
Every line in a new box needs corroboration when future frames are available.
A future-only companion must occur twice before enlarging the box. An
uncorroborated extra line cannot enlarge an established cue.
Each observation uses bar evidence from its own pixels, rather than retained
crop geometry. A change of bar bounds, source identity, queue continuity or
look-ahead policy breaks confirmation across that boundary. Loss of current
bar or text evidence clears the box. A queue with no available future frame
uses its current observation; the log distinguishes this fallback.
The diagnostic can recheck a subtitle-contaminated bar whose measured boundary
is already aligned with the clean opposite edge. This opt-in retains the native
pixel checks and connected-picture veto; normal crop acquisition is unchanged.
The detector does not authorize any source treatment.

`SUBTITLE BBOX` logs record cue, source frame, line count, geometry, current
observation versus held state, geometry revisions, trusted bar availability,
CPU cost and detected/held totals. A revision can be a real new cue or a late
correction; compare recording and source frames. Early means the first eligible
frame with qualified current bar evidence and available cue confirmation.
`current_scan_ms` reports the source observation cost; `window_new_scan_ms` and
`window_new_scans` report newly measured frames in the queue window. Cached
frames and cadence repeats do not add observations. `consume_ms` is only the
render-time tracking work. Per-line 64x16 ink-density signatures distinguish
changed strokes even when a long line retains the same outer bounds.

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
The line's horizontal center must lie within the middle 40% of the source
raster. This rejects isolated corner titles while allowing short centered cues.
Companion lines are checked above and below the original anchor with compatible
height, horizontal overlap and bounded spacing; they cannot form an expanding
chain of unrelated text. Off-center positioned subtitles remain outside this
trial's supported placement range.
This is a geometric eligibility gate, not proof that every accepted shape is text;
real-video false-positive validation remains required.

Detection samples at most 960x540, using an integer source stride. For 3840x2160
this is a 4-pixel stride on both axes (one sixteenth of the source pixel count).
Only bar/boundary strips are sampled; rendering stays full resolution and source
coordinates are restored for the outline. Logs report `analysis_step`. No OCR or
full-resolution subtitle scan is introduced by this diagnostic.

## Regression coverage and remaining limits

The unit tests cover both anchor orders, narrow centered words, corner titles,
cue onset/end, changed lines with an unchanged companion, transient extra lines,
AR changes in both directions, full-raster cuts, queue gaps, stale identities,
policy/viewport changes, cadence repeats, drift and current-glyph containment.
Config tests cover named-profile validation and saving/reopening the diagnostic.

`tools/test_subtitle_bbox_corpus.py` checks the spatial detector using labelled
antialiased captions over real movie backgrounds at HD and UHD. It draws bars
and text at the target resolution so their coordinates match the supplied
boundary. It does not exercise the renderer queue or establish live acceptance.
Bright picture detail can still merge with, obscure or imitate picture-side
glyph components, causing incomplete or oversized boxes. This trial remains
blocked for production relocation until representative playback meets the
complete-box, false-positive and consistency requirements.
