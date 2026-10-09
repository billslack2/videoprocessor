# VP-0070 subtitle consistency experiment

Story status remains **Blocked**. This is an isolated diagnostic experiment built
on the latest beta integration tip plus prior subtitle work. It does not
establish reliable recognition of arbitrary content.

## Detection and continuity

At least part of the actual text must be in a currently proved black bar. Padding
alone cannot qualify picture-only text. Analysis uses a grid no larger than
960x540, including explicitly sampled boundary rows. No OCR is used.

The padding guard evaluates the current frame and consecutive matching lookahead
frames once when a cue is acquired. Its margin remains fixed for that cue;
configuration, source-context, and cue changes acquire a new margin.

Picture-side components also require their own local black-backing support
before grouping and tracking. Keyed extraction is limited to the measured card
inside the picture, independently of the conservative glyph envelope. Glyphs
inside the letterbox remain eligible. The Whistling desktop replay is a
regression check only: its downscaled pixels do not reproduce the live UHD
sampling grid. UHD phase tests exercise adjacent moving scene strokes separately.

Supported input always has opaque black subtitle backing. Every accepted line
that touches the picture must show distributed black whitespace outside its
glyph envelopes; outlined letters or aligned scenery alone cannot qualify.
The original card's measured picture footprint is separate from glyph bounds
and destination padding. Removal covers that footprint through the picture/bar
seam, including portions outside the destination padding. Side searches that
reach their limit are not treated as measured card edges.

`tools/subtitle_black_panel_courtyard_manifest.json` records the original,
unprocessed interval of the four-mode courtyard capture. It tests both complete
two-line capture (including the leading dash) and exclusion of moving debris
above the shallow single-line card. Source-panel coverage and excessive removal
area have independent assertions. Rendered green/black/gray modes and seek UI
are excluded from detector inputs. These are interior cue checks, not an onset
latency qualification.

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
subtitle_box_padding_top: 40
subtitle_box_padding_bottom: 21
subtitle_move_inset: 0
subtitle_generated_gray_color: 040404
subtitle_generated_gray_opacity: 0.85
subtitle_generated_gray_blur_px: 3
subtitle_generated_gray_max_luminance: 0.16
subtitle_generated_gray_border_color: 000000
subtitle_generated_gray_border_opacity: 0.65
subtitle_generated_gray_border_width: 0
```

These defaults use a 60-pixel side buffer, a 40-pixel top buffer, and a
21-pixel bottom buffer. The top margin is reduced from 60 pixels to address
the oversized top edge, while the bottom gains one pixel to avoid clipping.
They affect both the green outline and moved panel, not raw eligibility or cue
identity. The larger margin contains detached leading marks and the visible
subtitle backing. Generated blend/black/gray panels round only their visible
upper corners; the lower edge is square and meets the active-picture boundary.
The upper radius derives from panel height and is capped at 28 source pixels.
The destination backing meets the active-picture lower edge by default;
21 pixels of bottom padding remains inside it below the glyphs. Set a positive
inset only when an intentional gap is wanted. Keyed modes separate
the detected content rectangle from that panel: padding never creates new glyph
candidates or expands glyph extraction. Generated-gray removes only the independently measured old card footprint. Rectangle
mode copies the whole padded patch. Bounds clip to the
source raster. Too-tall or non-upward placements leave the input unchanged.

The legacy experiment flags remain readable for older profiles but no longer
enable an overlay at startup. The two enabled movement modes display the
complete unwarped source raster for this experiment. During playback, **Ctrl+Shift+T** cycles normal configured playback,
generated-gray movement, black movement, then normal again. Startup and HDMI
resync always restore normal configured picture placement, including configured
subtitle-fit shifts. Experiment flags in older trial profiles do not enable
movement at startup. Green-box mode is no longer in the keyboard cycle.
Black and generated-gray background panels extend six additional source pixels
on each side; this does not change glyph placement, capture or source cleanup.

Legacy `subtitle_cut_paste_background` values remain accepted for diagnostic
compatibility. Runtime keyboard selection enables only `generated_gray` and
`black`; returning to normal uses the existing configured picture layout.
The diagnostic shader modes are:

| Value | Result |
| --- | --- |
| `rectangle` | Copy the entire current-frame padded patch. Vacated source is black. |
| `transparent` | Approximate keyed text over destination; smooth the original detected text area from current-row samples beside its padded rectangle. |
| `blend` | Same approximate cleanup, with a half-strength linear-light destination backing for SDR/PQ. |
| `black` | Keyed text over an opaque black destination panel. Vacated detected content is black. |
| `dark_gray` | Keyed text over an opaque panel at 8% reference white (16.24 nits for PQ). Vacated detected content is black. |
| `generated_gray` | Keyed text over a configurable rounded backing (default `040404`, 85% color opacity, 0.16 linear luminance knee). A reflected nearby patch with a side feather restores more picture texture into the old card; directional scenery may appear vertically reversed. |

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
Its style settings apply only in generated-gray mode: colors are six-digit sRGB
`RRGGBB` values without `#`; opacity is 0 (no tint) through 1 (solid color).
The luminance knee is Rec.709 Y in linear reference-white units, from 0.01
through 1.0. Increasing it retains more highlight detail; the smooth shoulder
compresses bright values instead of flattening all highlights to one level.
It affects only the panel backing, not moved white glyphs or nearby picture.
Border width is 0 through 8 source pixels, with 0 disabling the border; border
color and opacity are configured separately. Edit these keys in the
`[vprenderer]` section of `Subtitle-MOVE-GENERATED-GRAY.cfg`.

For the original text and dark backing that must be cleared inside the active
picture, the shader uses the same reflected patch with side feathering. This can
look vertically reversed in directional scenes and cannot recover hidden detail.
Generated cleanup covers the independently measured source card through the
active-picture boundary, irrespective of pixel brightness. Display padding
and the conservative glyph fringe do not authorize extra picture replacement;
the letterbox below the picture remains black. This uses one deterministic
method over the whole filled area and the current frame, without random seeds
or random texture selection. It cannot reconstruct detail hidden by subtitles
or black bars, and moving scenery can make the generated source cleanup change
over a held cue. True cue-frozen inpainting would require a separate persistent
frame/texture cache and validation at scene cuts and cue revisions.

`SUBTITLE MODE` logs the effective mode and padding; `SUBTITLE BBOX` reports cue,
frame, current/held state, geometry, bar authority and costs. All launchers start normal and retain their associated settings file. The
recommended `START-SUBTITLE.cmd` uses the generated-gray profile so its color
and opacity settings apply on the first toggle. Legacy launcher names no longer
force their diagnostic mode at startup.

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

### Source card cleanup and bright-scene defaults

Generated-gray cleanup uses the measured source-card rectangle clipped at the
active-picture boundary. Fill samples come from outside that measured rectangle,
preventing the original backing from contaminating them. Display padding affects
the destination panel, not removal or extraction authority. The measurement is
a conservative rectangular footprint, including a small sampling fringe; it is
not an exact rounded contour. An empty measured panel authorizes no cleanup in
the picture. The real letterbox below the picture remains black.

New trials use `subtitle_generated_gray_opacity: 0.85` and
`subtitle_generated_gray_max_luminance: 0.16`. The latter caps linear backing
luminance through a soft shoulder; white glyphs are excluded. Lower opacity shows
more picture texture; 1 makes the configured color solid. Existing trial folders
are not modified automatically.

### Larger captions and thin symbols

The detector now searches the outer quarter of the picture height at each bar
and allows components up to one eighth of the analysis height and caption rows
up to 98% of frame width. It still analyzes a reduced raster (at most about
960 by 540), limits component count, and requires actual glyph ink in a bar.
Thin horizontal marks survive initial component filtering so learned line-scale
and edge proximity can decide whether they belong to the caption. Retaining a
mark does not make it independent subtitle evidence.

Regression coverage includes thin leading dashes across every sampling phase at
multiple raster scales, large two-line captions with a much wider upper line,
and unchanged picture-only/corner-title/noise rejection. The larger-font screen
recording from October 6 at 09:44 was visually reviewed; its final paused cue
shows the leading-dash case. Its green diagnostic overlays and display scaling
make it unsuitable as an unmodified source-frame quantitative replay oracle.

The two-line truncation regression also covers short dialogue dashes that are
narrower than half the learned glyph height. These must attach to their own
baseline before the separate-row symbol pass runs; otherwise an upper row can
incorrectly borrow the lower dash and exclude the remaining lower row.
`tools/test_subtitle_large_font.py --probe x64/Release/SubtitleClipProbe.exe`
checks this with real font rasterization over six font sizes and all four UHD
sampling phases, using explicitly supplied synthetic picture boundaries.


## Configurable hold, shortcut and top-bar movement

`[vprenderer] subtitle_hold_ms: 250` bounds weak-current-ink retention in source
milliseconds (0 disables that grace; range 0–1000). Strict unchanged glyph proof
can retain a cue while it remains present. Clear disappearance, changed glyphs,
bar/format discontinuities and confirmed replacement cues still invalidate it.
Repeated output frames neither consume nor renew the budget. Existing queued
lookahead remains active; no additional capture queue latency is introduced.

`[shortcuts] subtitle_toggle: Ctrl+Shift+T` uses the existing accelerator parser,
conflict checks and config reload path. A blank value disables this shortcut.
This setting is file-only; no Config UI control was added. The cycle remains
normal configured playback → generated gray → black → normal. HDMI resync resets
to normal. Do not confuse movement retention with the separate screen policy
`subtitle_hold_seconds`, which controls picture fitting.

Top-bar cues move down; bottom-bar cues move up. The entire padded destination
must fit inside the active picture; oversized cues are rejected without clipping.
`subtitle_move_inset` is measured inward from the corresponding picture edge.
Corners facing the bar are square; picture-facing corners are rounded. Top-card
generated cleanup samples from below instead of reflecting the upper black bar.

Current trial settings (source pixels unless specified):

| Section | Key | Value | Meaning |
|---|---|---|---|
| shortcuts | subtitle_toggle | Ctrl+Shift+T | Cycle movement modes |
| vprenderer | subtitle_hold_ms | 250 | Weak-evidence grace, milliseconds |
| vprenderer | subtitle_box_padding_sides | 70 | Left/right display margin |
| vprenderer | subtitle_box_padding_top | 40 | Upper display margin |
| vprenderer | subtitle_box_padding_bottom | 21 | Lower display margin |
| vprenderer | subtitle_move_inset | 0 | Inward distance from picture edge |
| vprenderer | subtitle_generated_gray_color | 040404 | RGB hex tint |
| vprenderer | subtitle_generated_gray_opacity | 0.85 | Tint opacity, 0–1 |
| vprenderer | subtitle_generated_gray_blur_px | 3 | Whole-overlay background blur radius, source pixels; 0 disables, maximum 30 |
| vprenderer | subtitle_generated_gray_max_luminance | 0.16 | Linear luminance ceiling |
| vprenderer | subtitle_generated_gray_border_color | 000000 | RGB hex outline |
| vprenderer | subtitle_generated_gray_border_opacity | 0.65 | Outline opacity, 0–1 |
| vprenderer | subtitle_generated_gray_border_width | 0 | Outline width; zero disables |

Black/gray panels also have the existing fixed six-pixel side extension.
Legacy `subtitle_bbox_test`, `subtitle_cut_paste_test` and
`subtitle_cut_paste_background` remain accepted for compatibility; the runtime
cycle starts in normal mode regardless of these old startup flags. The normal
screen profile's `subtitle_fit` and associated picture-shift settings still
govern normal mode. Queue profiles' `active_picture_lookahead_frames` controls
available lookahead (the deployed VP profiles currently specify five).

`SUBTITLE STABILITY` in `C:\Videoprocessor\vp\logs\vp.log` records transitions only: detector
loss/recovery, visible-output loss/recovery, hold start/end, decision reason,
grace duration, cue/box, lookahead confirmation, geometry validity and counters.
`suspected_flash=1` means matching cue evidence reappeared after at most 500 ms
of missing output. It is a diagnostic heuristic, not proof of an incorrect
source disappearance. Repeated source frames do not generate duplicate events;
discontinuities reset tracking. No screenshot or text recognition is logged.


### Black-backed partial tracking

Acquisition remains strict. During the configured grace interval, an acquired
cue can retain its bounds with 70% of learned ink present on every line, or
60% when the detector also freshly measures the same source panel. Retention
requires 90% of learned black background samples in each horizontal quarter
of every line to remain black. Local glyph windows retain at least 25% of
their learned strokes; new strokes remain tightly limited and new detached
glyphs remain contradictory. This tolerates sampled stroke loss and an isolated
missing component without accepting a blank box or one surviving companion line.

Only strict evidence renews the grace budget. Partial tracking cannot acquire
or enlarge a cue. Confirmed changed text, missing lines, lost bar context and
discontinuities still override it. Logs identify this path as
`reason=grace-black-panel-partial-ink`. No additional configuration is required.

### Near-bar captions

`[vprenderer] subtitle_near_bar_px: 20` accepts opaque-black-backed subtitle
panel edges within 20 source pixels of a real top or bottom bar. Range: 0�200;
0 restores the previous overlap-only acquisition. Display padding never authorizes detection.
The distance uses input pixels, before zoom or scaling, and does not move the
measured bar boundary. Both lines of an accepted caption remain one cue.

Glyph extraction uses the union of accepted text-line extents with one sampling-cell
guard. The broader detection/cleanup fringe and display padding do not authorize
foreground pixels. This prevents bright picture seams beside the original card
from being copied as text into either black or generated-gray backgrounds.

Advanced relocation modes override classic subtitle picture translation and clear its retained hold/drift on every advanced-mode frame. Normal mode resumes configured classic handling; automatic aspect-ratio selection remains enabled. Extraction uses separate detected line extents, with no duplicate trailing sampling-cell expansion; display padding and original-card cleanup remain independent.

Full subtitle measurement now runs on one bounded CPU worker (at most eight pending jobs and sixteen ready results). The render path only polls exact-frame results; it never waits for measurement or runs a fallback full scan. Jobs retain source-buffer ownership through completion/cancellation. Strong independent positive measurements survive predecessor changes; tracking-dependent evidence still requires its original predecessor. Current measurement CPU duration remains in current_scan_ms; window_new_scan_ms and window_new_scans now count only synchronous work and should remain zero (worker_async=1). Near-bar cleanup covers the accepted short gap to the physical bar without expanding glyph capture.

An unchanged cue receives full spatial detection at approximately 10 Hz (source-frame cadence), with current sampled glyph/color comparison and fresh independent black-bar proof between full scans. Changed glyph samples, bar geometry, policy, discontinuity, or source identity force full detection immediately. New companions outside the existing line extents are discovered by the periodic full scan. This does not lower display FPS or cache video pixels.


### Auto-aspect relocation ownership (2026-10-07)

The 09-43-57 recording/log shows an already admitted 3840x1608 scope picture
(0,276–3840,1884) expanding to include source caption pixels through row 2042.
The raw proposal became provisional; clearing classic translation evidence left
presentation recovery retaining that caption envelope after the caption moved.

Relocation now composes before final viewport selection. Only successful composition,
matching current-frame retention and an already admitted picture contract permit a
bounded recheck of the affected top/bottom bar. The check excludes the actual erased
content rectangle, never display padding. All other bar pixels remain protected.
Safe remaining bands permit the existing picture crop and retire its obsolete
inspection/recovery state. Raw acquisition and transition evidence remain unchanged;
this grants no new crop authority, and initial acquisition still follows normal rules.

No new GPU pass, shader change, detector rescan, or configuration setting is added.
The additional presentation check reuses raw thresholds/evidence and samples only
the affected bar. `SUBTITLE ASPECT` logs report composition, exclusion and remaining
bar safety. HDMI playback still requires live verification.


### Adjacent marks and aspect-ratio resilience (2026-10-07)

The supplied ellipsis screenshot reproduced a line ending at x=673 while its last
dot ended at x=703. The updated recording also showed a detached closing quote.
Finite adjacent mark groups now acquire the existing line's ownership before
line bounds, temporal signatures, extraction, and cleanup are calculated.
A group remains anchored to original lettering, requires local opaque backing,
and cannot turn a continuing noise chain into a subtitle. Tiny marks survive
component filtering but cannot independently seed a caption.

Aspect retention is separately resilient to incomplete capture. After successful
relocation on an already admitted picture contract, a bounded neighborhood may
contain a few small neutral residual marks without triggering outward fit.
Native-pixel component size, total ink, density, color, and perimeter checks
limit this exception; remaining UI elsewhere and actual picture expansion still
veto retention. The raw aspect-acquisition evidence is never replaced.
`SUBTITLE ASPECT` adds `residual_tolerance` to distinguish this proof.
No new configuration is required. Arabic-style raster tests are script-neutral
proxies, not a claim of complete Arabic-video qualification.

### Advanced subtitle / AR handoff correction (2026-10-07)

Advanced placement now fits inside the generation-current AR picture rectangle.
The subtitle detector's independently sampled bar edges no longer have to match
that rectangle exactly. Successful composition plus a current remaining-bar proof
retains the previously admitted picture; failed composition, stale identities,
full-raster authority, genuine transitions, and other bar content still veto it.
An obsolete classic caption-fit flag no longer disables optional aspect fill once
relocation has independently proved that the excluded bands are safe.

Mode-switch tests exercise normal -> gray -> black -> normal -> gray, top and
bottom captions, and mismatched subtitle/AR edge sampling. Negative tests cover
failed composition, stale measurement/epoch/generation, an uncontained destination,
full-raster observations, and unrelated UI. No configuration or shader changes.

### Caption display stability (2026-10-07 afternoon)

Reviewed recording 2026-10-07 12-10-15.mp4 with dense samples and per-frame glyph
bounds around the final caption. Its accepted geometry stayed fixed; changes
between different caption strings and mode changes are not same-cue jitter.
The source/destination log nevertheless exposed a 21-source-pixel downward dock
for a bottom near-bar caption already inside the picture. Extend only its
bar-facing display margin to prevent this reverse movement (mirrored at top).
Measured opaque-card width is now a minimum display width, and a stable cue's
display envelope cannot shrink as card edges are remeasured. New cues, padding,
context, and continuity reset it. Capture masks and cleanup never inherit the
cached display envelope. Regression coverage includes logged 4K geometry, both
edges, backing wider than glyph padding, and current-only cleanup after shrinking
measurements. No AR or detector threshold changes.

## Reconstruction blur (2026-10-07)

`subtitle_generated_gray_blur_px` accepts 0 through 30, including fractional values,
in `[vprenderer]` or a display profile. Default 3 is a source-pixel radius (its
visible size follows picture scaling). Zero disables blur. The generated backing
now defaults to near-black `040404` with opacity `0.85`; existing explicit settings
continue to override defaults.

The whole overlay background now uses a smooth separable Gaussian, not a sparse
3x3 grid. It first prepares the combined real/reconstructed scene in a small
linear-light texture covering the overlay, source cleanup and filter halo.
Horizontal and vertical Gaussian passes then sample adjacent texels with
Gaussian weights (sigma = radius / 3). Bilinear paired taps reduce fetches without
introducing gaps. Final composition applies the existing tint, highlight shoulder,
crisp border and crisp glyphs. Both real and reconstructed content are filtered.

The prepared region is full resolution below radius 4 and half resolution for
wider blur, using an exact 2x2 area reduction. Its grid is aligned to source pixels
to avoid changes when the region moves by one pixel. These three small background
passes are skipped for zero blur, disabled relocation and other background modes.
The existing composition pass samples the finished Gaussian once. There is no
readback or CPU processing of image pixels. At radius 30 each Gaussian axis needs
17 texture fetches per half-resolution region pixel, rather than a dense 2D kernel
at full picture resolution. Live HDMI performance still requires user validation.

Samples remain within the physical picture and sanitize source-card sampling
footprints before filtering. The output mask confines blur to the overlay and
measured source cleanup; detection and aspect-ratio evidence are unchanged.
Shader-hook sizing and conditional saved passes follow the
[mpv/libplacebo hook format](https://mpv.io/manual/master/#options-glsl-shaders).

This is softened nearby texture, not recovery of the actual hidden scene.
Directional scenery can still look reflected. The shader source changed once for
this update, so that variant may compile once; the radius is a dynamic parameter
and changing it does not generate new shader source or clear the shader cache.

### Asynchronous measurement continuity (2026-10-07)

The 15:30 recording/log shows accepted captions alternating with unanalyzed
worker results (`bar_reason=invalid-source`). Resolving an unavailable result
formerly discarded its identity and immediately reset the accepted cue; telemetry
also reset on the missing bar evidence, hiding these losses from flash counters.

The pending-result guard now preserves frame identity and bridges at most
`subtitle_hold_ms` (currently 250 ms) using fresh native-pixel checks of the
accepted glyph rectangles and fresh independent bar boundaries. Any sampled
luma/chroma change greater than 8/1023, changed bars, discontinuity, policy/viewport
change, skipped source frame, or expired budget rejects the bridge. Completed
negative measurements still release immediately. Repeated presentations do not
renew grace. Full detector work stays asynchronous. This bounded fallback reads
at most 32,768 glyph-region samples plus the existing bar proof, without OCR,
spatial search or GPU readback. Gaussian blur and aspect acquisition are unchanged.

Automated tests cover stable cue identity during late results, expiry, repeats,
changed glyphs, blank frames and viewport/discontinuity rejection. Actual HDMI
playback and GPU pacing still require verification on the deployed build.


### Subtitle performance telemetry (2026-10-07)

Five-second `SUBTITLE PERFORMANCE CPU` windows report full scans and cheap
fingerprint refreshes separately: counts, actual Hz, average/peak wall-clock
execution cost, completed worker busy milliseconds, queue wait, dropped jobs,
request misses, and verified pending frames. Request misses include lookahead
queries; they are not dropped video frames. Render-thread consume timing includes
current-pixel pending checks and display geometry, but excludes composition.

`SUBTITLE PERFORMANCE GPU` reports black and generated-gray composition separately,
including reconstruction, blur and full-source intermediate rendering. GPU timestamps
use the existing delayed nonblocking D3D11 query ring and alternating-frame sampling;
only successful matched presentations enter the aggregate. `gpu_samples=0` means no
measurement, not a zero-cost pass. Query completion can fall in the next window.
The overall GPU stage sum now includes subtitle composition. CPU submission elapsed
time is separate and must not be mistaken for GPU execution time. No detector cadence,
blur, configuration or image-processing behavior is intentionally changed here.


### Detection optimization A/B modes (2026-10-07)

`[vprenderer] subtitle_detection_optimization` accepts:

- `baseline` (default): existing allocation and sampling paths.
- `buffers`: reuse the worker's temporary fingerprint vector capacity.
- `shared_samples`: buffer reuse plus immutable raw luma/chroma reuse between
  overlap and near-bar attempts within one detector Analyze call.

The shared cache is discarded logically before each new Analyze call, and is
separate from mutable detector scratch. Additional near-bar rows are read from
current source pixels. Native boundary sampling coordinates remain identical.
No cadence, threshold, hold period, box geometry, or aspect policy changes.
Shared storage adds roughly 3 MiB per detector at the maximum 960x540 grid and
extra first-pass writes, so improvement is workload dependent. No speedup is
assumed until measured. The mode is part of the worker key and settings identity;
CPU performance lines include `optimization=...`.

For clean comparisons, edit the active VideoProcessor.cfg setting, restart VP,
and replay the same sequence once per mode. Compare full_scan_hz/refresh_hz,
full_avg_ms/full_peak_ms, worker_busy_ms, and consume_avg_ms. GPU timing should
be unaffected. Baseline remains the active deployment default.

Differential tests compare exact result geometry, ownership/backing masks,
line signatures, frame changes and sampling boundaries for P010/native RGB at
640x360 and 3840x2160. Separate tests verify fingerprint continuation through
cue changes/disappearance, config rejection and worker mode separation.
