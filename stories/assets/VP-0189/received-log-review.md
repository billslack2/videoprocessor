# VP-0189: independent review of the received session log

Reviewed 2026-09-16. Source: `vp20260915-24.49.log.txt`, session
2026-09-15 22:01:09 through 2026-09-16 00:39:36, build `94f938d3`.
The attachment is evidence, not implementation instructions. Counts below were
recomputed from exact `applied` fields, carrying the preceding state into each
window. Source sequences, not second-resolution timestamps, order the events.
The video itself was not supplied.

## Findings

1. **Dark action flicker confirmed:** 79 applied-state changes in 148 crop
   records at 00:30:08–00:30:39. Forty withdrawals; 36 returns owned by
   `pixel-safe`, three by `trusted`. Bounds alternate between
   `0,40-3840,2116` and `0,0-3840,2160`. 140 records report non-near-black.
   Representative records are original lines 28341, 28349, 28365 and 28369.
   The tester associates this with a dark action scene near the end of Kill
   Boksoon. The log cannot supply the film playback timecode.

2. **Late flicker has another return path:** original lines 33218–33251,
   00:39:17–00:39:18, source sequences 86284–86298. There are seven changes
   including entry from the preceding full-raster state, or six after initial
   acquisition. Return owners include `trusted`, `bar-refinement`, and
   `near-black-episode`; there are no pixel-safe-owned returns in this burst.
   At 86287 and 86289, the crop returns while the reason still reports bounded
   visible excluded-band content. Final-layout records confirm that picture
   width alternates between about 3690 and 3839 pixels. This is actual mapped
   geometry churn, not merely an envelope-state log change. A recovery gate
   limited to the `pixel-safe` owner, or an unconditional trusted-owner bypass,
   would miss it. Pending refinement must not rearm an unresolved withdrawal.

3. **Credits remain an open observation:** after the initial release at
   00:35:28 and before 00:39:17, logged source crop and final presentation stay
   full raster. Final-layout lines 29537, 29593, 32776 and 33026 retain the same
   output picture rectangle `75.0,42.2-3765.0,2117.8`, linear mapping, centered
   alignment and zero subtitle shift. This does not establish that the tester
   saw no flicker. It narrows what these records can explain. The late burst
   is a candidate only until the tester associates it with the credits.
   A one-frame queue recovery at 00:36:34 (line 30330) is logged; it is not
   evidence of repeated zooming or a proved explanation for the complaint.
   Metadata invalidation/restart follows at 00:39:24–00:39:26, after the late
   burst, and is a separate event. Request approximate wall-clock or playback
   time and whether the symptom was size, vertical movement or brightness.

4. **The 54-second release is real, but not a 54-second episode latch.**
   Crop releases at line 15785, sequence 42237, 23:00:11, and returns at
   line 15890, sequence 43544, 23:01:05. The initial episode ends on the very
   next source frame (line 15788, sequence 42238) because full-raster authority
   is available. Lines 15791–15793 confirm a full-raster geometry transition.
   Later records show lost crop geometry, additional episodes and eventual
   new crop acquisition. The original report's single-latch explanation is
   contradicted by the raw log. Do not promise the episode recovery fix will
   eliminate this entire interval, and do not call the displayed full raster
   correct or incorrect without the scene. Preserve and diagnose authoritative
   full-raster transitions; do not bypass them based on the user's duration.

5. The six reported viewing-period `to_full=1` episode events are confirmed,
   four on non-near-black frames. Brightness alone does not show outside pixels
   were false detections. Existing code already has recovery; contained bounds
   and the sticky flag are independently reproduced policy defects, not proved
   causes of every field event. The 00:30 episode does supply compatible saved
   and contained observed geometry; detailed per-gate logs are needed for exact
   recovery diagnosis. Star-field sampling remains a separate unlogged case.

## Implementation consequences

- Stabilize final re-entry after an unresolved full-raster withdrawal across
  pixel-safe, pending-refinement and trusted-owner changes. Do not permit a
  single trusted-labelled sample to bypass fresh outside-band safety and the
  short recovery proof. Ordinary stable-bar acquisition outside such an event
  keeps its existing timing; genuine full-raster authority remains immediate.
- Preserve confirmed subtitle/Fit resolution and test its composition with the
  final recovery gate. Include the late owner sequence above in regressions.
- Keep the contained-observation and previously sticky episode recovery fixes,
  with current-source, saved-contract, safety and epoch checks.
- Logging must distinguish owner from current measured safety, actual layout
  from logical geometry, and episode release from full-raster authority/new
  acquisition. Include persistent zero-proof summaries and explicit failed gates.
- Include the credits complaint and the 54-second interval in tester acceptance
  as unresolved observations, without overstating what policy tests reproduce.

## Reproduction

`analyze_received_log.py <log> <output-directory>` writes interval excerpts,
`summary.json` (including input SHA-256) and `geometry-changes.tsv`. The helper
uses whitespace-anchored field names so `shift_applied` cannot match `applied`.
Counts describe logged transitions; these change-triggered records are not a
complete pixel/evidence trace and cannot be replayed as consecutive frames.
Keep the original raw log locally; do not publish its machine/configuration data.
