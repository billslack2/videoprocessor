# VP-0070 Step 1 bounding-box diagnostic

Enable `subtitle_bbox_test: true` in `[vprenderer]`, then restart VP.
The flag also works in a named Rendering profile such as
`[vprenderer.profile_1]`; Config may rename the root when saving profiles.
Default is false. This trial uses VP Renderer.
Select the physical 16:9 viewport. While enabled, presentation fits the complete
input raster with no crop, NLS warp or subtitle translation. It does not change
saved zoom, screen, or color settings. Disable the flag and restart to return.

A three-output-pixel bright green outline encloses the detected cue, with configurable source-pixel padding (30 top/sides, 10 bottom by default). It is
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
crop geometry. A dedicated diagnostic proof samples 128 distributed columns,
learns the neutral black level, and finds the edge of each flat band. It checks
boundary contrast across a short transition window so resampled edges remain
usable, while rejecting the tested smooth scene gradients. Broad contiguous
picture intrusions invalidate the affected bar even when most of that band is
black. Top and bottom are proved independently: asymmetric and single bars are
supported. This proof replaces the native crop analyzer's authority for the
subtitle diagnostic only; it never authorizes cropping or changes normal AR
acquisition.

A measured boundary may differ by one source row between matching observations
because of a resampling fringe. Both frames still need fresh bar proof and the
same presence or absence of each bar. A two-row boundary change, loss of an edge,
source identity change, queue discontinuity or look-ahead policy change breaks
confirmation. The cue's acquisition reference also bounds accumulated drift.
Loss of current bar or text evidence clears the box. A queue with no available
future frame uses its current observation; the log distinguishes this fallback.

`SUBTITLE BBOX` logs record cue, source frame, line count, geometry, current
observation versus held state, geometry revisions, current bar availability,
CPU cost and detected/held totals. A revision can be a real new cue or a late
correction; compare recording and source frames. Early means the first eligible
frame with qualified current bar evidence and available cue confirmation.
`bar_authority`, `picture` and `bar_reason` describe the geometry used for this
frame. `bar_depth`, `bar_support`, `bar_black_fraction` and `bar_samples` expose
the proposed edge depths, supporting columns, verified flat-band fractions and
sampling work. A valid opposite edge can remain usable after one edge fails;
`picture` identifies which bounds actually received authority.

`current_scan_ms` reports the source observation cost, including diagnostic bar
proof and text detection. `window_new_scan_ms` and `window_new_scans` report
newly measured frames in the queue window. Cached frames and cadence repeats do
not add observations. `consume_ms` and `peak_consume_ms` cover render-time
tracking only. `future_available` and `matching_frames` distinguish queued
confirmation from immediate current-frame fallback. `analysis_step` and
`work_limit` expose spatial sampling and the bounded-component guard.

Per-line 64x16 ink-density signatures distinguish changed strokes even when a
long line retains the same outer bounds. Signatures include only pixels of
components assigned to that line; unrelated scenery or another line inside its
rectangle cannot change its fingerprint merely by occupying the same envelope.

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
Character scale is learned from a plausible centered group of substantial
components whose ink enters a bar. A differently sized corner title cannot
set the scale for the centered caption. Even one sampled bar pixel qualifies
when it belongs to a substantial letter stroke; isolated noise cannot.
Line grouping then checks compatible component size, baseline, overlap and
spacing before admitting picture-side strokes. Small dots and punctuation can
attach afterward, only close to the original substantial letters; they cannot
seed a line or form a growing chain of bright scenery fragments. If too few
bar glyphs establish scale, conservative geometric grouping remains available.
Companion lines are checked above and below the original anchor with compatible
height, horizontal overlap and bounded spacing; they cannot form an expanding
chain of unrelated text. Off-center positioned subtitles remain outside this
trial's supported placement range.
This is a geometric eligibility gate, not proof that every accepted shape is text;
real-video false-positive validation remains required.

Detection samples at most 960x540, using an integer source stride. For 3840x2160
this is a 4-pixel stride on both axes (one sixteenth of the source pixel count).
Only bar/boundary strips are sampled. The last top-bar row and first bottom-bar
row are sampled explicitly so a single crossing glyph row cannot fall between
stride positions. Rendering stays full resolution and source coordinates are
restored for the outline. A small padding envelope covers antialias fringes and
terminal punctuation, but padding never establishes bar eligibility.
Logs report `analysis_step`. No OCR or
full-resolution subtitle scan is introduced by this diagnostic.

## Regression coverage and remaining limits

The unit tests cover both anchor orders, narrow centered words, corner titles,
cue onset/end, changed lines with an unchanged companion, transient extra lines,
AR changes in both directions, full-raster cuts, queue gaps, stale identities,
policy/viewport changes, cadence repeats, drift and current-glyph containment.
Additional bar-proof tests cover asymmetric and single bars, odd-height RGB,
limited-range luma, sparse crossing glyphs, continuous dark gradients, dark
texture, connected picture intrusions on both edges and bounded UHD work.
The one-row context tolerance has paired tests for stable geometry and rejection
of missing bars or larger boundary changes. Config tests cover named-profile
validation and saving/reopening the diagnostic.

`tools/test_subtitle_bbox_corpus.py` checks the spatial detector using labelled
antialiased captions over real movie backgrounds at HD and UHD. It draws bars
and text at the target resolution so their coordinates match the supplied
boundary. It does not exercise the renderer queue or establish live acceptance.
The actual recording replay in [VP-0070_PHANTOM_REPLAY.md](VP-0070_PHANTOM_REPLAY.md)
exercises production bar proof, spatial detection, look-ahead confirmation and
presentation using the recorded pixels. Its fixed annotations, clean/stress
separation and independently annotated complete cues complement the synthetic
spatial corpus. Those cues have informed iteration, so they are no longer an
unseen validation set. A recorded
desktop raster does not recover the original 4K input or its exact frame identity.

Bright picture detail can still merge with, obscure or imitate picture-side
glyph components, causing incomplete or oversized boxes. This trial remains
blocked for production relocation until representative playback meets the
complete-box, false-positive and consistency requirements.

See `VP-0070_CUT_PASTE_TRIAL.md` for the current per-frame tracking contract, backing modes and padding settings.
