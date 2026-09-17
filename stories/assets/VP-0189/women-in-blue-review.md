# VP-0189: Women in Blue playback review, 2026-09-17

The supplied 911e2b7f log supports the reported repeated crop jumps. The user
identified **Apple TV, Women in Blue, S02E06, 10:55–11:30**. The episode identifier
is recorded as supplied. The tester reports that madVR follows the transition
correctly; we have not independently played the passage yet.

Raw file: `vp - Kopie.log.txt` (3,270,456 bytes).
SHA-256: `cbf0464de54721d8aa2d39ac9dd1f5603479d2d875e4cf376ccb01e527da0cf2`.
Reproduce the extraction with `review_women_log.py`; the resulting
`women-in-blue-evidence.json` preserves original line numbers and source sequences.
This is a sparse diagnostic review, not a reconstruction of every video frame.

## Does the duration match?

| Log-clock interval | Source sequence interval | Elapsed source time at 24000/1001 |
| --- | --- | --- |
| 05:27:38–05:28:10 | 3856–4634 | 32.449 s |
| 05:29:03–05:29:35 | 5897–6668 | 32.157 s |
| 05:34:17–05:34:49 | 13434–14205 | 32.157 s |
| 05:43:15–05:43:47 | 7915–8686 | 32.157 s |

These spans are consistent with the approximately 35-second passage. They start
at logged crop decisions, which can lag the first visible movement. The 05:29
and 05:34 passes have identical crop changes at identical relative source-frame
positions: 0, 237, 422, 423, 577, 632, 662, 771. Their two full-frame holds last
6,422 ms and 4,547 ms each. This strongly supports a reproducible content-driven
case; exact player-time-to-log alignment still needs tomorrow's playback.

The separate 111,782 ms and 108,437 ms recovery events at 05:38:45–05:40:36
and 05:40:39–05:42:27 must not be presented as the duration of that 35-second
format animation. They expose another failure mode in the same session.

## Confirmed conflicts and corrections

1. **Measurement tolerance versus exact crop reaffirmation.** The transition
   model intentionally retains `0,208–3840,1948`, while fresh bar observations
   repeatedly say `0,208–3840,1952`. There are 99 logged summaries with that
   mismatch and safe excluded bands. The final recovery gate rejects exact
   containment; the inspection latch can independently keep its candidate at
   full raster. Examples start at line 7313. Both owners must agree that a safe
   sampling-sized difference can reaffirm the same crop. No new aspect ratio,
   source rectangle, or inward crop is published for that four-pixel difference.

   The shared rule requires valid matching bar axes and a current excluded-band
   safety measurement of the exact retained contract. Edge and total-size
   differences must fit sampling equivalence: max(2, width/480, height/270),
   eight pixels at 4K. This is much smaller than the 2% geometry deadband.
   Actual outside pixels still veto recovery. Freshness, generation, epoch,
   full-raster authority, and the seven/sixteen-sample recovery dwell remain.
   Near-black entry recovery uses the same equivalence, with the raw detector
   classification: episode acquisition suppression must not erase evidence
   needed to recover the unchanged saved crop.

2. **Horizontal conflict versus valid outward presentation.** At lines
   5486–5487, source sequence 13857, a right-edge conflict discards a crop
   immediately after sequence 13856 accepted it. The raw observation is still
   a contained trusted bar crop and the log reports bounded outside pixels;
   the renderer also computes an outward Fit envelope. The policy previously
   rejected all horizontal conflicts before considering that envelope.

   Accept the outward Fit only when a same-source-frame pixel certificate
   bounds every unsafe edge, belongs to the exact trusted base, and the final
   envelope covers both its visible extents and the current detector geometry.
   Missing, stale, partial, unaligned, or contradictory proof still falls back
   to full raster. Translation and explicit fail-open retain priority. An
   already armed recovery event cannot use this to skip its inward proof.
   The log lacks the detailed pixel extents needed to prove every reported
   occurrence would pass the new gate; normal and bounded trace diagnostics
   will distinguish this in playback.

3. **Moving nested geometry restarts its own confirmation.** Continuous bar
   observations with moving coordinates repeatedly replace the candidate and
   restart its four-second clock. Lines around 5340–5367 show the progression.
   Keep the existing dwell across nearby, current, same-axis authoritative
   nested observations, while updating the candidate to current bounds.
   Missing evidence, a large jump, or a scene reset breaks the proof. This
   changes confirmation bookkeeping, not the general deadband or black level.

4. **Candidate provenance survives replacement incorrectly.** Starting a
   novel candidate could retain the prior candidate's `knownTrustedGeometry`
   flag. Clear that flag whenever starting a candidate; the history path can
   explicitly set it again. A regression covers recent-crop interruption by a
   genuinely new geometry.

5. **Diagnostics hid the end of long recovery events.** Ordinary changes reused
   a closed event ID with duration zero. They now use event zero; the helper
   also handles older logs without overwriting the completed event. Report
   actual fill rectangles, candidate reason, inspection latch, sampling
   reaffirmation, accepted horizontal pixel proof, and nested candidate age.
   Reuse existing image measurements; no new scans or configuration changes.

## Four seconds and the overall approach

Four seconds is our policy choice, not an industry requirement. Commit a0f43278
introduced it to reject brief newly nested windowboxes; existing tests protect
a three-second transient. Ordinary same-axis changes use adjacent confirmation,
not this dwell. Tests encode the intended tradeoff but do not prove four seconds
is optimal for all material. VP-0136 also records that a broad eight-second rule
was reverted after delaying genuine Eternals transitions. Do not repeat that
mistake by adding a blanket dwell.

The correction retains the existing nested guard for this comparison build.
Its timing and the remaining broad-deadband steps should be judged in playback.
We deliberately removed an experimental fine-tracking mode because it could
chase small alternating edge noise. Do not advertise perfectly smooth animated
cropping or complete resolution of this passage before watching it.

Primary-source comparisons, checked 2026-09-17:

- [mpv's bundled autocrop example](https://github.com/mpv-player/mpv/blob/master/TOOLS/lua/autocrop.lua)
  measures for one second and then applies a crop; its four-second startup delay
  is not continuous transition hysteresis.
- [FFmpeg cropdetect](https://ffmpeg.org/ffmpeg-filters.html#cropdetect)
  estimates bounds. By default it retains the largest detected picture until
  reset, and quantizes dimensions. It does not supply a universal live crop policy.
- [The third-party mpv dynamic-crop implementation](https://github.com/Ashyni/mpv-scripts/blob/master/dynamic-crop.lua)
  stabilizes nearby measurements and separates familiar-geometry confirmation
  from slower novel-geometry validation. Its defaults differ by evidence type;
  they are examples, not timing requirements for VP.
- [MPC-HC's maintainer discussion](https://github.com/clsid2/mpc-hc/issues/3209)
  treats the discussed automatic bar detection as a madVR renderer function.
  It does not establish an MPC-HC detection algorithm or four-second standard.

The appropriate direction is consistent meanings for stable logical geometry,
current pixel safety, presentation ownership, and bounded recovery. Preserve
normal successful behavior; correct contradictions before tuning sensitivity.

## Playback on 2026-09-18

Use the same source/output settings and subtitle state as the report, including
the 2.133:1 bottom-aligned screen and wider-content fill if still intended.
Replay S02E06 from about 10:45 through 11:40, preferably twice. Note the log clock
at playback 10:55 and identify whether remaining movement is gradual scale
tracking, full-frame flashing, or subtitle motion. Keep the full raw logs.

Compare the currently deployed 911e2b7f with the follow-up candidate. For detailed
existing edge measurements, launch with `VP_CROP_TRACE_FRAMES=600`; the trace is
bounded and now includes horizontal conflicts even when their outward Fit is
accepted. No persistent configuration edit is needed. Also check stable bars,
real full-height changes, subtitles, dark fades, and a multi-AR star field.
Fixed-grid sampling can still miss isolated stars; do not call this star recognition.
