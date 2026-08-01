# VP-0066-9: Converge the VP-owned queue once per fresh live-output epoch

## Status

In Progress (2026-07-31). This child follows the live evidence from
VP-0066-6. It is not a madVR queue controller and never runs during ordinary
steady playback.

## Parent and dependencies

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

Dependencies: the epoch-owned transport and DirectShow lifecycle work in
VP-0066-3/4, and the fresh output epoch/readiness contract in
VP-0066-6. The work must retain madVR as the DirectShow downstream renderer.

## User story

As a live-capture viewer, I want VP to establish the configured small,
deterministic VP-owned queue after every graph start or reset without dropping
timeline time or chasing madVR's unobservable internal queue.

## Evidence and problem

With `steady_reserve_frames: 2` correctly applied, a 59.94 Hz live run began
delivery with five VP preroll samples. Two immediately subsequent synchronous
`Deliver()` calls blocked for 109.7 ms and 123.5 ms. Conversion continued at
the capture rate, creating a 13-frame VP converted-queue lead. Thereafter
source and delivery both ran near 59.94 Hz, so an equal-rate pipeline cannot
drain that lead on its own.

The 2026-07-31 controlled run proved that convergence itself is already fast:
the initial epoch trimmed 29 to 2 in 656 ms, and subsequent active epochs
trimmed 13/14 to 2 in 328-360 ms. The apparent roughly 3,000-frame delay was
the DeckLink global capture counter spanning repeated graph epochs, not time
spent in the convergence controller. Two avoidable liveness resets occurred
after transient-invalid capture-state notifications stranded renderer ingress:
the next accepted counters jumped 1404 to 1794 and 2900 to 3107. Each resumed
frame was then incorrectly treated as proof that the graph was dead, causing a
full DirectShow reset and draining madVR's queues.

The rejected hard-cap experiment removed already timestamped samples without
rebasing their timeline, producing VP drops and madVR repeats. The correct
solution must either preserve a continuous output timeline while stale live
content is skipped, or decline the convergence; it must never create a
timestamp hole merely to report a lower OSD queue number.

## Required behavior

- Arm exactly once for each fresh DirectShow pipeline epoch: initial graph
  start; successful output-readiness, manual, liveness, renderer, display, or
  HDMI-recovery reset/restart; and no other event.
- Do not run in normal playback after an epoch has converged. A future graph
  epoch is the only re-arm condition.
- Preserve the existing prompt first-picture path. It is acceptable for early
  live content to stutter or be skipped, but VP must not wait an arbitrary
  five or ten seconds before showing a picture.
- With a nonzero `[queue] steady_reserve_frames`, wait for the initial
  downstream priming, observe a genuine synchronous `Deliver()` block, and
  then observe three recovered deliveries before requesting one VP-owned
  convergence to the desired converted-queue depth. Fast early `S_OK`
  deliveries alone are not readiness evidence.
  Delivery duration is only a one-sided local startup-settlement signal; it
  must not be represented as madVR queue occupancy or a feedback API.
- The synchronous-block threshold is the greater of three nominal frame
  periods and 30 ms. The nominal frame duration is available immediately at
  epoch start so ordinary 23.976-Hz delivery is not misclassified as a block.
- Convergence may remove only stale converted work, and only while the raw
  queue is known and empty. The configured value is a converted-queue reserve,
  not a target for total OSD R/C/T depth or madVR's private queues.
- If no block is observed within three seconds, recovery is not completed
  within two seconds after the block, the target changes in the same epoch,
  raw depth is nonzero, or display-cadence scene mode is active, fail closed:
  do not trim and record the reason.
- A zero/automatic queue policy remains untouched.
- Final DirectShow presentation/media timestamps must be owned at the delivery
  boundary, or an equivalent serialized rebase must prove that removing stale
  converted samples yields a strictly increasing continuous sample timeline.
  Pixel conversion remains asynchronous and no extra queue, frame copy, or
  worker is introduced.
- If continuity cannot be proved, do not trim; log the blocked reason and
  retain the elastic queue rather than causing repeats.
- A transient invalid capture-state notification that retains the last valid
  renderer state must retain frame admission atomically. It must not silently
  turn the 1.5-second grace policy into a multi-second ingress outage.
- A newly received frame after a source-counter gap proves that capture has
  resumed. Re-baseline cadence measurement, mark a DirectShow source
  discontinuity, and preserve the running graph. A counter gap alone is not
  sufficient evidence for a full madVR-draining liveness reset.

## Testable increments

1. **Policy (implemented):** `LiveEpochConvergenceController` is a pure C++14
   value state machine. It is disabled for automatic policy, requires an
   observed startup `Deliver()` block followed by three recovered deliveries,
   and requests at most one stale converted-frame convergence per epoch. It
   re-arms only for a new epoch. Native tests replay the measured 59.94-Hz
   startup trace (109.8/124.785-ms stalls followed by
   17.897/16.518/16.515-ms recovery), prove the 13-to-2 convergence, prove that
   23.976-Hz startup is classified correctly, and cover timeout, raw-nonzero,
   scene-mode, target-change, idempotence, and fresh-epoch behavior.
2. **Timestamp ownership/rebase (implemented; awaiting live validation):**
   `RationalLiveOutputSequencer` is now the single delivery-thread owner of
   final presentation and media timestamps for both normal Rational-Rational
   and display-cadence scene delivery. Cadence/PPM changes rebase at the last
   committed stop without creating a new epoch. Preview is side-effect free;
   commit occurs only after `S_OK`. Renderer-gap repeats consume presentation
   slots without double-advancing media, and source/epoch discontinuities are
   preserved. Tests cover normal/display/normal transitions, failed delivery,
   renderer gaps, source discontinuity, exact 60000/1001 cadence, and a
   four-hour 24000/1001 display-cadence run.
3. **One-shot integration (implemented; awaiting live validation):** the
   DirectShow delivery thread observes each delivery outcome and once per
   fresh epoch may trim oldest converted work to the explicit `[queue]` target
   after proven block/recovery and raw-zero evidence. `S_FALSE` is treated as
   downstream rejection: the sequencer does not commit and delivery remains
   latched until a new epoch/flush. Optional deferred-repeat failure abandons
   the clone without a renderer reset. A dedicated `*-convergence.csv`
   preserves startup proof across later periodic exports, and the manifest
   records the policy, thresholds, pre/post/discard depths, raw-zero evidence,
   timestamp owner, and the explicit fact that madVR occupancy is unobservable.
4. **Display validation:** exercise 59.94 SDR first, then 23.976 and 59.94
   HDR. Retain madVR OSD captures as passive evidence only. Verify a small,
   repeatable VP R/C/T after each relevant reset/restart while madVR remains
   normally primed and no sustained VP/madVR drop or repeat regression occurs.
5. **Transient-state admission recovery (implemented; awaiting live
   validation):** invalid capture-state publication now records ordering while
   atomically retaining the current renderer admission. Valid state changes
   still close ingress until the renderer explicitly acknowledges them.
   Resumed forward gaps and counter resets are converted into source
   discontinuities, reset the PPM measurement window, and continue without a
   graph reset. Telemetry records publication latency and
   published/required/acknowledged admission sequences. Native tests reproduce
   the 1404-to-1794 incident, prove that retained invalid state never strands
   ingress, and prove that an unapplied valid publication superseded by a
   retained invalid publication reopens safely.

## Acceptance criteria

- Unit tests cover startup prime, startup stalls, normal delivery, target
  already met, automatic policy, one-shot behavior, fail-closed boundaries,
  timestamp/media ownership, and rearm on a new epoch. The current native
  suite passes 354/354 tests in x64 Release.
- A convergence never happens in an unchanged steady epoch.
- Normal, no-trim delivery preserves monotonic 60000/1001 and 24000/1001
  timestamps exactly within existing documented rounding tolerance.
- Trimmed live startup content never creates an overlapping, backward, or
  gapped DirectShow presentation/media timeline.
- No added queue, frame copy, worker-thread hop, capture-callback wait, or
  madVR API/OSD control input is introduced.
- Live validation logs a deterministic VP target and pre/post VP depth for
  each fresh epoch, while explicitly leaving madVR occupancy unknown.
- On the known 59.94-Hz monitor, VP reaches the converted target within one
  second of the first current-epoch downstream delivery and reaches a stable
  state comfortably inside 15 seconds after valid HDMI format acquisition.
  HDMI acquisition time itself is reported separately rather than attributed
  to VP convergence.
- A retained transient-invalid state produces no `liveness-recovery` graph
  reset. Any source-counter discontinuity is logged with `graph_reset=0`, PPM
  rebaseline, and continuous DirectShow presentation/media timestamps.

## Controlled validation process (tonight)

1. Begin with the known 59.94-Hz SDR monitor and the existing active
   `[queue] steady_reserve_frames: 2`. Do not replace the active configuration.
2. Start VP or rebuild the graph and observe at least 90 seconds. First
   picture remains prompt; convergence is not allowed to hold video for a
   fixed five- or ten-second readiness delay.
3. For each fresh epoch, expect at most one convergence decision:
   - if a real startup block and recovery are observed with raw depth zero,
     telemetry records the state transition and one planned trim with exact
     target, pre-depth, post-depth, and discarded count;
   - if the evidence is absent or unsafe, a `ConvergenceState` record with a
     reason such as `block-observation-timed-out`, `raw-depth-not-empty`, or
     `unsafe-boundary` and no `PlannedDrop` is the correct fail-closed outcome.
4. Treat the VP OSD R/C/T and madVR OSD as observations, not as control inputs.
   The configured `2` applies only to VP converted reserve; madVR queue fill
   remains passive evidence because no supported occupancy API is available.
5. Exercise initial start, manual reset, and a source switch that rebuilds or
   resynchronizes the output. Check for no new multi-second black screen, no
   repeated autonomous reset loop, continuous video after recovery, and no
   sustained VP or madVR drop/repeat growth.
   - A transient-invalid notification must show
     `published == required == acknowledged` with admission open.
   - A later counter gap, if any, must show `action=continue-local`,
     `ppm=rebaseline`, and `graph_reset=0`; it must not be followed by a
     `liveness-recovery` reset.
   - Record first valid-format time, first picture, first convergence trim, and
     stable target separately. The target should be reached in under one
     second after delivery begins and the full known-monitor experience should
     remain comfortably under 15 seconds after valid format acquisition.
6. Stop or rebuild once to flush telemetry. Review the latest
   `*-convergence.csv` together with its manifest; do not infer startup state
   solely from a later one-minute rolling delivery CSV.
7. After SDR 59.94 passes, repeat at HDR 59.94. Validate 23.976 on the real
   compatible display; a 23.976 source on the current 59.94-only monitor is a
   cadence-mismatch negative fixture, not a queue-convergence acceptance run.

## Out of scope

Continuous madVR queue chasing, IQualityControl emulation, scraping the madVR
OSD, reading madVR settings/rules as a control input, a global PLL, arbitrary
user-configurable delays, and changing normal steady-state delivery cadence.
