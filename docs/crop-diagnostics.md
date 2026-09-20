# Automatic crop recovery and diagnostics (VP-0189)

This candidate starts from local commit `3acd02b3`, including unmerged VP-0188.
It repairs unresolved crop re-entry and near-black recovery; it does not retune
bar/near-black thresholds or identify stars from sparse samples.

## Behavior

An unresolved ordinary crop withdrawal arms final presentation recovery. Full
raster remains visible until the current crop has safe excluded bands, a valid
contained observation, compatible current source/measurement/epoch and an
eligible presentation owner on adjacent independent source frames for 250 ms.
The count is `ceil(source_fps * .250) + 1`: seven frames at 23.976/24 fps,
sixteen at 59.94/60 fps. Gaps restart partial proof; repeats do not advance it.
Unsafe, unavailable, near-black or incompatible evidence resets partial proof.
There is no timeout that crops uncertain picture just because time elapsed.
The bound applies after evidence qualifies, not to the duration of a dark scene.

Pixel-safe, trusted and pending-refinement labels cannot individually bypass an
active recovery event. Normal acquisition outside that event keeps its existing
timing. Full-raster authority, disabling automatic crop and context invalidation
end the old recovery context. Confirmed current dense subtitle translation/Fit
or completed near-black crop proof resolve it without a second dwell. Pending
inspection/translation/Fit alone cannot rearm a withdrawal. Fixed-aspect crop
remains an explicit operator override. NLS uses full raster while recovery waits.

Near-black recovery still restores only the saved trusted crop. The current
observation may be contained inside that crop instead of exactly matching every
edge; it cannot authorize a deeper crop. Previously confirmed outward content
can be revalidated once current safe evidence returns. Changing presentation
epoch discards the old entry certificate and restarts native bootstrap; it cannot
reuse partial proof from the former profile. Existing paused bootstrap remains.

## Records

Normal diagnostics need no additional setting:

- `Alpha crop diagnostics`: schema, dwell, summary interval, sampling/threshold
  configuration and bounded trace budget, once per renderer instance.
- `Alpha crop recovery`: meaningful source-crop/owner transitions, event start/
  end, episode boundaries and a summary every two seconds while unresolved.
  `stage=source-crop` is final crop arbitration before subsequent NLS mapping.
  `prev_applied`/`applied`, previous/new bounds and `actual_change` distinguish
  state/bounds changes from owner/evidence changes. `previous_available=0` is an
  initial snapshot, not a measured transition. `candidate_owner` explains which
  owner would otherwise have restored crop. Separate `bands_safe`, proposal
  availability/containment, currentness, full authority and gate names explain
  why proof advances or stays zero. Event counters include resets/evidence flips.
- `Alpha final layout`: actual presentation and output mapping, including
  `crop_event`, epoch, measurement sequence and cadence repeat. Use this to
  investigate movement when the source `applied` flag stays unchanged.
- Existing `Alpha source crop` and near-black episode records remain available.
  Source-crop log deduplication now tracks presentation/owner changes instead of
  every raw evidence flag. Proof counters and detailed traces are observations,
  not fresh crop authority.

For bounded per-edge detail, launch a candidate from PowerShell with:

```powershell
$env:VP_CROP_TRACE_FRAMES = '600'
& 'C:\path\to\candidate\VideoProcessor.exe'
```

This is a process environment diagnostic, not a `.cfg` setting. It allows at
most 600 `Alpha crop edge trace` records per renderer instance, only during
unresolved events on fresh source evidence. Smaller positive budgets work;
omitting it disables detail. Restarts create a new renderer instance/budget.
Remove it from that shell afterward with
`Remove-Item Env:VP_CROP_TRACE_FRAMES -ErrorAction SilentlyContinue`.

Edge detail reuses existing sample data: bar depth, black fraction, P90, texture,
continuity, sample totals and measured outward bounds. Unsupported spatial
support counts are explicitly `unavailable`. There are no additional scans for
logging. These samples cannot tell whether a detected point is a star, subtitle
or noise. Trace records are capped; ordinary summaries continue after exhaustion.
The plugin does not inherit the host logger's enhanced-logging Boolean, so this
bounded environment control avoids an ABI or persistent configuration change.

## Analysis

Python 3, standard library only:

```powershell
python tools\analyze_crop_log.py 'C:\path\to\vp.log' `
  --start '2026-09-16 00:30:08' --end '2026-09-16 00:30:39' `
  --output crop-summary.json
python -m unittest discover -s tools -p test_analyze_crop_log.py
```

The helper uses exact field names (`applied` never matches `shift_applied`),
carries preceding state into an interval, separates source-crop from final-layout
changes, detects sequence restarts, groups diagnostics by session/generation/
epoch/event and includes scene identity on source transitions. Gate counts are
counts of records, not per-frame frequencies. Missing gate/edge evidence is
reported explicitly. Change-triggered logs cannot be replayed as every frame.

`capture_missed` is an estimate from hardware timestamp gaps; it is not independent
proof the card failed to deliver frames. `renderer_dropped` counts queue removal.
Neither counter alone explains crop behavior; their arithmetic is unchanged.

## Tester passages and remaining validation

The received `94f938d3` log verifies 79 source-crop changes at 00:30:08–00:30:39
and seven at 00:39:17–00:39:18 including initial acquisition. The first passage
is associated by the tester with dark action near the end of Kill Boksoon.
The later burst includes trusted/refinement returns, not just pixel-safe returns.

The 23:00:11–23:01:05 release is verified, but the original episode ends on the
next source frame; full-raster authority and later acquisition follow. This
repair is not proof that the entire 54-second observation will disappear.

Credits flicker remains open. Logged crop/output mapping stay constant from
00:35:28 until the late burst; this does not disprove a visual symptom or identify
its cause. Please give approximate wall-clock/playback times, whether the symptom
was zooming/movement/brightness, title and edition, and subtitle/overlay state.
Return the whole raw session log (including rotated files if needed) and exact
candidate build identity. Also test stable bars, genuine full-height passages,
return to letterbox, subtitles and a repeatable multi-AR star-field passage.

Moving-star fixtures cover several sample-grid phases and a detectable small
bright cluster outside the crop. Subpixel/isolated stars can still fall between
the fixed sampling grids; there is no blanket star-field recognition claim.
Live renderer overhead, visual quality and physical source playback require the
tester run; native policy/pixel tests alone cannot certify them.

## Gradual-format follow-up (2026-09-17)

Returned tester case: Apple TV, Women in Blue, S02E06, 10:55–11:30.
Four repeated crop sequences in `vp - Kopie.log.txt` span 32.16–32.45 seconds,
consistent with the supplied approximate 35-second passage. Separate recovery
events last 111.782 and 108.437 seconds; those are not the animation duration.

Sampling-scale differences can reaffirm an unchanged crop only with matching
bar authority and current safe excluded bands. The equivalence bounds each edge
and aggregate dimension difference by max(2, width/480, height/270): eight pixels
at 4K. This does not publish tiny aspect changes, loosen black thresholds, or
replace the 2% static-geometry deadband. Both general and near-black recovery
keep source/epoch checks and the existing quarter-second inward proof.

Horizontal conflicts can use an outward Fit only when complete current pixel
extents belong to the exact trusted base and the resulting envelope contains
those extents and current detector geometry. Unproved conflicts remain full
raster. This does not bypass an already active recovery or subtitle translation.

Nearby continuous affirmative nested observations retain their candidate start
time as coordinates move. The existing four-second guard for a new cropped axis
remains; ordinary same-axis changes retain their existing confirmation behavior.
Neither that delay nor the unchanged broad deadband is claimed optimal for
animated formats before playback. No fine per-frame aspect tracking was added.

Additional schema-1 fields (additive):

- `sampling_reaffirmed`: current bar geometry agrees within sampling tolerance
  and the retained rectangle's measured bands are safe.
- `horizontal_bounded`: the candidate accepts a current fully bounded horizontal
  outward expansion; does not mean a pre-existing recovery was bypassed.
- `inspection_latched` and quoted `candidate_reason`: identify the owner before
  recovery arbitration, including why it already requested full raster.
- `candidate_first`, `candidate_age_frames`, `nested_guard_ms` on active-picture
  decisions: explain sustained nested proof and its age.

Ordinary changes outside unresolved recovery now use event zero. The helper
preserves closed-event durations in older logs and includes `fill_rect` in source
changes. `VP_CROP_TRACE_FRAMES=600` also captures horizontal conflicts whose Fit
was accepted; it still has one bounded budget and uses existing measurements.

For the 2026-09-18 comparison, replay about 10:45–11:40 twice, note the wall clock
at 10:55, preserve subtitle/output settings, and retain complete logs. Compare
full-frame flashing, gradual scale steps, and subtitle motion separately. Also
exercise normal stable bars, genuine full-height transitions, dark fades and
multi-AR star fields. The tester report used a 2.133:1 bottom-aligned screen with
wider-content fill enabled. Visual acceptance and live overhead remain pending.


## Subtitle transition admission diagnostics

`VP build identity` records the host and renderer commit URL, branch, dirty flag and build description. Each module uses its own generated identity. The plugin-loaded line supplies the DLL path; deployment records supply the matched binary hashes.

`Alpha crop admission` reports explicit deferral, presentation/outward reasons, broad-picture proof and confirmations, raw/logical bounds, publication and history reacquisition. It logs deferral changes and at most one summary every two seconds while deferred. This distinguishes a raw subtitle-affected proposal from permission to change the logical aspect. No trace environment variable is needed.

The subtitle-onset regression keeps the logical picture stable across remembered full-height proposals at 23.976/24/59.94/60 Hz. Explicit deferral blocks both local history and queued publication; each newly exposed edge requires current picture evidence. Recovery proof, black thresholds and nested-crop timing are unchanged. The recorded failure is prevented before the false logical publication can arm full-raster recovery; physical playback is still the acceptance test.

## Provisional scan-step retention (2026-09-19)

Both Andrew's vp73c85004.log.txt and the local session identify the same
remaining failure on clean host/renderer build 73c85004. This build contains
the crop updates through PR #97; the intervening PR #98 concerns packaging.

Andrew's log contains 21 recoveries with retained bounds
0,276-3840,1884, provisional bounds 0,276-3840,1888, safe sampled
excluded bands, and no outward-visible evidence. Their durations are
281-297 ms. The local log also shows this signature, including at 19:13:13,
19:13:29, 19:24:30 and 19:24:54. Locally the vertical-inspection fallback
also latches before recovery. The full-raster proof interval is the visible
flash; the initial unnecessary withdrawal is the defect.

The captured raster remains 3840x2160 with anamorphic scale 1.00000.
The four-row difference is one acquisition step, changing inferred picture
aspect from 2.38806 to 2.38213 (about 0.25%). These logs do not establish a
pixel-aspect-ratio change or identify the exact source pixels causing the scan
to stop early. Whole-bar sample statistics alone cannot prove every disputed
row is safe.

A provisional vertical proposal can now retain an existing opposing-bar crop
when width is unchanged, each vertical edge and the aggregate height difference
stay within one acquisition step, and the current excluded bands remain safe.
The disputed outward rows receive an additional 256-position check per row,
using the existing credible-luma cutoff and chroma tolerance. Bright or colored
samples veto retention. This adds at most 1,024 source samples at 4K and uses
the format-independent analysis source interface.

This is permission to retain the existing rectangle only. Raw classification
stays provisional; no new crop authority is granted. Larger conflicts, absent
trusted bar axes, genuine visible expansion, recovery timing and detection
thresholds retain their existing behavior.

Additive fields in recovery and source-crop logs:

- sampling_retained: this provisional one-step rule proved retention safe.
- sampling_strip_conflict: the focused disputed-row check rejected retention.
- Existing sampling_reaffirmed continues to describe trusted-geometry
  equivalence; it has not been repurposed.

Source-crop logging includes changes to these fields even if the final rectangle
stays steady. No extra logging option or configuration change is required.

Regression validation first reproduced rejection of the pixel-safe crop and
the joined inspection/recovery fallback on the unmodified beta. The correction
passes those fixtures, preserves the provisional observation, and rejects
bright/colored narrow strips and larger expansions. Additional cases cover
resolution scaling, lack of trusted axes, and combined two-step expansion.
Fixtures reproduce the logged decision signature, not captured source pixels.
Replay of the episode on the candidate build remains necessary for visual
acceptance and live performance validation.

Validation: x64 Release solution build succeeded; all 1,298 native tests passed.
The two original reproductions failed before the correction and passed after it.
Test artifacts: %TEMP%\vp-pixel-safe-20260919\scan-step-red.trx and
scan-step-full.trx. The candidate has not yet been deployed for replay.

## Shared presentation tolerance follow-up (2026-09-19)

The initial 318cec4a candidate above was deployed with matching clean host and
renderer identities at 19:37. Live replay still produced the same 1884-to-1888
withdrawals at 19:45:17 and 19:45:29. Both records had safe broad bar samples but
sampling_strip_conflict=1. Thus rejecting any bright/colored disputed-row sample
was too conservative; the initial candidate did not resolve this playback case.

The revised rule separates two facts: whether the sampled excluded pixels look
black, and whether the boundary discrepancy warrants a presentation change.
CanRetainPresentation accepts either strict pixel safety or the bounded
provisional sampling equivalence. The latter requires an already trusted
opposing-bar crop, unchanged width, safe broad excluded bands, and at most one
acquisition step in each vertical edge and total height. At 4K this intentionally
tolerates up to four source rows of edge content without changing framing.
It does not relabel those pixels as black. Larger/other-axis discrepancies and
measured outside-band content retain their existing behavior. The reference
rectangle is never advanced by this rule, preventing cumulative drift.

The same provisional-equivalence interpretation now feeds ordinary retention,
vertical inspection resolution, and ordinary recovery proof. Once recovery is
active, fresh matching samples may count toward its existing dwell even though
the raw geometry remains provisional. Source generation/sequence, exact retained
base, raster, classification, visibility, ownership and near-black gates remain.
No new trusted aspect is published and no confirmation timer was enlarged.

A new recovery regression failed before this consolidation: current retainable
geometry produced zero qualifying frames. A bright one-row fixture also failed
before the edge-tolerance change, reproducing the new live diagnostic signature.
Negative cases cover stale and mismatched evidence, unsafe bands, changed width,
larger expansion, cadence repeats, unrelated presentation ownership and epoch
reset. Existing pixel-conflict assertions are retained separately from the
intentional presentation tolerance.

Additional log fields: sampling_equivalent, sampling_strip_peak_y, and
sampling_strip_peak_uv_delta (10-bit source code units, chroma distance from 512).
sampling_strip_conflict remains an honest pixel diagnostic and is not by itself
a veto of a one-step border. sampling_retained still identifies strict strip
proof. retention_safe describes permission to retain presentation, including
this bounded tolerance.
