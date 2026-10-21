# VP Renderer chroma and Motion configuration

Targets `v1.4.01-beta` and `dv2`; native HDR output is deferred.

## Scaling

`chroma_upscaler` belongs to each `[vprenderer.scaling]` profile. Missing values
resolve to `bicubic`, independently of rendering quality. There is no Auto value.
Supported values are `bilinear`, `bicubic`, `lanczos`, `ewa_lanczos`, and
`ewa_lanczossharp`. The selected filter is passed to libplacebo's plane_upscaler.
It reconstructs subsampled color planes even at native image size; RGB input
has no chroma reconstruction step. The existing sigmoid control is labeled
Sigmoid upscaling to distinguish it from a separate filter anti-ringing strength.

## Motion profiles

The compact Qt tab row adds Motion between Subtitles and Processing. The
Motion page uses the existing ordered profile, shortcut, and conditional-rule
model. Controls appear before shortcut metadata so they are visible at 1040x700.
Changing motion configuration restarts the renderer to discard temporal history.

```ini
[vprenderer.motion]
smooth_motion: off
frame_mixer: oversample
deinterlace: auto
deinterlace_algorithm: bwdif
field_order: auto
deinterlace_cadence: field
deinterlace_skip_spatial: false
```

- `smooth_motion`: `off`, `auto` (verified source/display cadence mismatch),
  or `always`. Default Off. Matching integer multiples bypass Auto mixing.
- `frame_mixer`: `oversample`, `hermite`, `linear`, or `cubic`.
  Default Oversample. This blends frames; it does not run RIFE/optical flow.
- `deinterlace`: `auto`, `off`, or `force`. Default Auto. Auto requires positive
  interlace metadata and known field order. Force still requires reported order
  or an explicit override; unknown order is not silently treated as top first.
- `deinterlace_algorithm`: `weave`, `bob`, `yadif`, or `bwdif`. Default BWDIF.
  Weave leaves the original frame intact. Bob is the missing-reference fallback.
- `field_order`: `auto` (From input), `top`, or `bottom`.
  DeckLink field dominance and completed Magewell-frame metadata are retained.
- `deinterlace_cadence`: `field` or `frame`. Default Field. Field mode preserves
  both fields when the display supports that rate; output is bounded by the
  measured display cadence. Frame mode selects one field-derived image/frame.
- `deinterlace_skip_spatial`: boolean, default false; applies to YADIF only.

## Runtime boundary

Off/progressive matched-rate output retains the single-image path without
allocating temporal history. Active motion uses up to eight private GPU frame
slots, recycled after warmup, and at most sixteen outputs per source interval.
The mixer receives monotonic relative PTS, stable frame signatures, and display
interval duration. YADIF/BWDIF receive ordered previous/current/next frame planes.
Mixing/deinterlacing dispatch through the existing NLS hook recovery contract.

History is cleared on source discontinuity, generation, scan/order policy,
substantial display-rate change, transfer change, swapchain reconstruction, and
backlog recovery. Source buffers are released through the original queue owner;
only copied GPU textures are kept. Missing texture capabilities/allocation
fall back to the ordinary path and report the reason.

Motion adds buffering: the OSD presentation status and log identify the active
method, field order and retained-frame delay. Audio alignment and live playback
quality require operator review; no live capture playback was changed during
implementation. Black/Reconstruction subtitle composition retains motion and
deinterlacing. Ordinary target overlays remain active.

### Source measurements and output intervals

The OSD separates source input interval from scheduled output interval. For
23.976 fps input mixed to 75 Hz, it shows 41.71 ms and 13.33 ms respectively.
Source interval uses measured input cadence with known-format fallback. Output
interval remains the GPU-budget percentage denominator; this change does not
increase the allowed output cost or change playback scheduling. Unknown input
rates remain unavailable. GPU percentages cover the existing sampled timestamp
stages, not all CPU/GPU preparation, Present waiting or scanout.

One source's output loop owns a temporary pixel-measurement cache. Repeated
outputs can reuse raw global-darkness, active-picture and presentation-retention
observations from the same immutable analysis input. The key includes complete
source identity and analysis domain, live source layout/interpretation, pixel
pointer/extent, backing-owner pointer and ownership identity, plus policy and
continuity generations. Retention also requires the exact presentation bounds.
Missing ownership or an incomplete identity disables reuse; context changes invalidate
observations, and leaving the source's output batch discards the cache.

Only pixel observations are cached. Existing per-output policy evaluation,
current worker evidence, crop admission, evidence leases and presentation timers
retain their existing behavior. Cached evidence is returned by value so those
updates cannot modify the saved raw measurement. Rendering, temporal history,
frame mixing, NLS and output processing keep their existing ordering.

### Subtitle composition with motion

Each accepted source frame is captured once, including frames with no scheduled
output. The existing detection and AR rules operate on the original analysis
view. A split of the existing subtitle shader produces a repaired full-raster
RGB16F picture and an independent-alpha subtitle layer in source transfer.
Frames without a cue use the same RGB normalization, so cue changes cannot
switch the representation or output clock. Configured chroma reconstruction,
its anti-ringing strength and debanding run on the source planes before RGB
conversion. These settings are snapshotted with each packet; final output does
not apply debanding a second time. Final image scaling and sigmoid processing
retain their configured output-stage settings. Dolby conversion is consumed once;
normalized RGB packets contain no borrowed Dolby matrix pointers. Native field
packets own their Dolby metadata, so recycling the shared matrix cache does not
restart composition history.

Progressive history owns at most eight repaired pictures and copies only the
occupied subtitle-layer rectangle. Reusable upload/composition textures are not
borrowed. For interlaced input, history instead owns native planes and a snapshot
of each cue's geometry, style and optional background donor. Libplacebo processes
native YUV/RGB fields with ordered neighbors before RGB conversion and cleanup;
this preserves its chroma and YADIF/BWDIF ordering. Prepared progressive fields
are cached only for the mixer's support window. Bob is the existing fallback
when required neighbors are unavailable; those cached fields are refreshed when
neighbors arrive. The cropped glyph layer comes from the complete source packet,
so a field does not erase fine subtitle marks.

The clean progressive pictures are mixed into a linear RGB16F intermediate.
The most recent subtitle packet at or before the output timestamp is applied
there with linear-light alpha blending. An explicit empty packet clears a prior
cue. During warmup, display holds the previous complete image until a packet is
eligible; it shows neither a future cue nor a cleaned picture missing its cue.
Glyph layers are not blended across cues. Source field preparation runs before
display acquisition, so a partial packet cannot be presented or trigger a retry
with unprocessed source.

Final crop, NLS, output color mapping, calibration and OSD run after this
composition. Source policy is evaluated once for each accepted source frame,
including frames without a scheduled output. A bounded journal retains the
admitted crop, NLS geometry, caption candidates and HDR protection with that
source's identity. Output uses the journal entry for the selected delayed
picture, rather than geometry from the newest captured frame. Repeated display
draws reuse source policy; they must not renew detection evidence or subtitle
leases. Source preparation and selected geometry must both be ready before
acquiring a display buffer, so a missing packet holds the last complete image.

Ordinary Classic translation evaluates a copy of its saved drift at each output
PTS, bounded to the selected source interval. This changes the presentation pose
without advancing source evidence. NLS and fixed-crop paths retain their source
policy pose rather than extrapolating geometry through a competing transform.
Mixer neighbors must share the selected picture's geometry family. Differing
Classic crop poses require current pixel-safe band evidence; missing or stale
proof does not authorize mixing. Incompatible deinterlacing neighbors use the
existing Bob fallback. Subtitle mode and viewport policy changes invalidate the
presentation journal; a size-only output change refits frozen source geometry.

Capture, normalization or split-composition failure rejects an incomplete packet
before display acquisition and retains the last complete presentation, with a
diagnostic reason. It never silently switches to cue-dependent bypass. The next
source can reprime history. Later GPU/output failure retains the established
swapchain recovery policy. The ordinary non-composition path retains its existing
fallback.

This adds full-raster conversion/composition work and RGB16F history memory.
At UHD, eight progressive clean pictures alone use about 506 MiB, plus subtitle
ROIs, working surfaces and libplacebo caches. Interlaced memory also includes up
to eight native packets and prepared RGB fields in the current mixer window;
fields outside that window are released. Live GPU timing/memory and audio sync
still need qualification on the target hardware; the pixel tests are not a
playback performance benchmark.

## Validation

Tests cover configuration parsing/validation and persistence, exact libplacebo
option mapping, submitted field order, source texture ownership, generation
reset, bounded scheduling, successful GPU calls for every mixer/deinterlacer,
and navigation across all eight tabs. GPU pixel regressions cover discrete cue
selection over mixed pictures, empty-cue clearing, warmup, repeated outputs,
crop/rotation, partial alpha in both subtitle modes and SDR/PQ/HLG transfers.
Native RGB and P010 field results are compared with direct libplacebo references
for Bob/YADIF/BWDIF and both field orders. Additional checks cover chroma filter
preservation, copied Dolby metadata, immutable source settings and failure
recovery. These tests preserve rendering contracts; they do not rank libplacebo's
visual quality or replace live performance and audio-sync checks.


## Presentation priority and analysis timing

Optional lookahead raster extraction and raw darkness scans use an owned, bounded
CPU worker. Jobs retain their prepared input and publish only to that captured
frame's cache, with the analysis domain checked again before use. Missing or late
work does not become negative AR/subtitle evidence. Only a contiguous ready prefix is advertised as available lookahead. A proof
requiring more frames cannot use an unmeasured tail or bridge a missing frame. Live current-frame safety rules remain active.

The render thread admits the remaining preview work against a fixed QPC deadline.
Qualified DXGI sync timing supplies the display phase; the budget reserves recent
render cost before the next refresh and caps optional work at 2 ms. When timing
is unavailable or stale, only a 1 ms dispatch/cache window is allowed. Repeated
frames and an already backed-up queue get no optional preview budget. Checks
between work units and before proof publication abandon expired work without
moving its deadline into another interval. These are cooperative checks, not a
hard real-time guarantee for the duration of a single inspection or driver call.
The media PTS clock is not stepped to chase DXGI timing, and the interpolation or
deinterlacing algorithm's required history delay remains unchanged.

Explicit cadence corrections reuse the last successfully submitted output PTS.
They do not reset temporal history, advance source evidence, or consume the next
ordinary output tick. A repeat during initial warmup has no eligible submitted
output and is not counted as a presented repeat. Generation, settings, field-order
and genuine source-discontinuity changes still invalidate temporal history.

Presentation records keep incoming source sequence (the existing cadence/debt
key) separately from selected source sequence/PTS, output PTS, cue sequence, and
presentation epoch. Every record is associated with its submitted present ID.
The observed DXGI present ID may refer to an earlier submission; the periodic
output-correlation log labels both, and pacing history preserves exact selected
identity for each retained record. The log also reports preview elapsed time,
allowance, qualified phase, and whether a complete window was published.

Subtitle-assisted AR remains an independent current-frame pixel proof. It may
exclude measured subtitle backing or owned crossing glyphs only for that exact
source identity, analysis domain, policy, continuity, and script. Held cues,
missing scans, stale worker output, and gaps between separate cards are not masks.
Existing format-parity tests cover native packed input, P210, P010, and decoded DV.


Analysis performance diagnostics emit `VP analysis performance` every two seconds
of successful submissions. Preview attempts/completions/skips and fixed-deadline
overshoots are counted across the interval, with average and peak costs. Active
presentation-path cost includes preview/live analysis, lock wait and render work;
it excludes intentional queue waits, parallel conversion time and blocking swap.
Live preparation is also reported separately, including analysis, upload and
any waits encountered inside render preparation; these are wall times, not CPU
times. Preview is charged once when a source produces multiple outputs.
`source_period_exceeded` compares that cost with capture cadence. The separate
`display_period_exceeded` comparison is diagnostic headroom, not proof of a
missed physical refresh (24p on 60 Hz has different source and display periods).
Existing GPU query telemetry remains authoritative for measured GPU costs.

The single lower-priority optional worker reports submitted/completed/failed,
stale cancellation, duplicate, contention and full-queue counts. Queue wait,
job wall time and Windows thread CPU time distinguish backlog, CPU execution
and descheduling. CPU samples can be coarse; unavailable CPU timing is exposed
by sample coverage. Worker totals belong to jobs completed in the window, so
cross-window jobs and approximate atomic snapshots must not be interpreted as
instantaneous CPU utilization. `worker_active_age_ms` exposes unfinished scans;
queue high-water and worker peaks explicitly have lifetime scope. Logical
processor count supplies machine context, not a request to occupy every core.
This instrumentation has bounded atomic/clock costs, no extra pixel scans and
no worker locks on the presentation path. It never changes detector decisions.


### Selected Scope continuity with configured aspect fill (2026-10-15)

The 10:40:38 recording exposed a presentation handoff regression: native Scope
`0,272–3840,1884` was displayed with configured 2.35 horizontal fill
`26,272–3814,1884`. The selected-output anchor rejected the aspect difference,
so the next subtitle onset had no submitted Scope anchor and latched full raster.
The clean-base release had the same exact-base versus configured-fill mismatch.

The selected temporal packet now carries its immutable crop admission. Both
anchor and clean-base commits accept an exact native source rectangle or that
selected source's explicit horizontal-fill witness. Generation, epoch, sequence,
native contract, actual rectangle and unchanged vertical boundaries must match.
An equal aspect ratio alone is not proof of the displayed source rectangle.
Completed native clean-source proof can also nominate a retained episode's base;
retention ownership alone still cannot acquire a new crop.

Regression coverage includes the recorded caption onset through bounded crop
admission, delayed selected output versus newer live evidence, missing/stale fill
witnesses, shifted same-aspect crops, failed submissions, and genuine full-raster
authority. Positive outside-pixel evidence retains logical Scope but requires a
complete visible envelope (or temporary full output if none can be proved).
Missing evidence alone does not retire the submitted Scope contract.

`Alpha submitted Scope anchor` reports availability changes and throttled failed
anchor creation, including selected identity, actual/native bounds and fill
identity. It does not add a frame scan or wait to rendering.


### Declared range and global darkness

Analysis samples retain their existing native ten-bit code values. The prepared
live and queued analysis sources now carry the validated full/limited range into
global darkness classification; the interpretation domain version is incremented
so old observations cannot cross this policy boundary. Limited-range and legacy
unknown-range sources retain the P90 cutoff of 96 (32 codes above nominal black
64). Full-range sources use the equivalent normalized signal interval,
`32 * 1023 / 876`, approximately 37.37 codes above black 0. This is code-range
normalization of the darkness decision, not a tone map or a claim that DV
opponent intensity, YUV luma and every transfer function are photometrically
identical. Pixel values, bar/edge thresholds, glyph rules and renderer input are
unchanged.

Native diagnostics record the declared range and exact cutoff. Capture header
field 11 is optional: 0 Unknown, 1 Limited, 2 Full. Legacy ten-field recordings
remain readable with Unknown behavior; never infer a recording's range merely
from its darkest pixels. Ordered capture/replay retains the same source identity
and range, and source-range changes invalidate interpretation-dependent history.

When exact current worker evidence corrects a subtitle-biased AR candidate,
expansion strips are remeasured for that candidate before transition admission.
Raw native evidence and excluded-band visibility remain untouched. A corrected
rectangle must not borrow strip measurements from the previous raw candidate.
