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
  downstream priming and then for the startup `Deliver()` stalls to cease
  before requesting one VP-owned convergence to the desired frame depth.
  Delivery duration is only a one-sided local startup-settlement signal; it
  must not be represented as madVR queue occupancy or a feedback API.
- A zero/automatic queue policy remains untouched.
- Final DirectShow presentation/media timestamps must be owned at the delivery
  boundary, or an equivalent serialized rebase must prove that removing stale
  converted samples yields a strictly increasing continuous sample timeline.
  Pixel conversion remains asynchronous and no extra queue, frame copy, or
  worker is introduced.
- If continuity cannot be proved, do not trim; log the blocked reason and
  retain the elastic queue rather than causing repeats.

## Testable increments

1. **Policy (implemented):** `LiveEpochConvergenceController` is a pure C++14
   value state machine. It is disabled for automatic policy, waits for five
   successful downstream samples, then three deliveries no slower than two
   nominal frame periods. It requests at most one stale VP-frame convergence
   per epoch and re-arms only for a new epoch. Native tests replay the observed
   109.7/123.5 ms 59.94-Hz stalls and prove the ten-frame request from depth 12
   to target 2, non-request at target, and fresh-epoch rearm.
2. **Timestamp ownership/rebase (implemented; awaiting live validation):**
   `RationalLiveOutputSequencer` now provides a pure, delivery-owned preview /
   commit sequence for the deployed Rational-Rational path. Its native tests
   prove exact 60000/1001 continuity, no output-time gap when stale picture
   content is skipped, retry-after-failed-Deliver, and epoch reset behavior.
   A 59.94-Hz production trace confirmed 968 successful deliveries with a
   strictly contiguous Rational timeline (180 ms lead, then exact
   166,830/166,831-tick cadence). The delivery thread now applies this
   sequence to normal Rational-Rational samples immediately before `Deliver`
   and commits it only on success. Active P010 scene cadence remains its own
   delivery-thread timestamp owner. This increment does not yet discard any
   queued picture content.
3. **One-shot integration:** let the DirectShow delivery coordinator execute
   the proven transaction only for the current epoch. Log target, pre/post
   VP-owned depth, skipped stale content count, output sequence, and the
   explicit fact that madVR occupancy is unobservable.
4. **Display validation:** exercise 59.94 SDR first, then 23.976 and 59.94
   HDR. Retain madVR OSD captures as passive evidence only. Verify a small,
   repeatable VP R/C/T after each relevant reset/restart while madVR remains
   normally primed and no sustained VP/madVR drop or repeat regression occurs.

## Acceptance criteria

- Unit tests cover startup prime, startup stalls, normal delivery, target
  already met, automatic policy, one-shot behavior, and rearm on a new epoch.
- A convergence never happens in an unchanged steady epoch.
- Normal, no-trim delivery preserves monotonic 60000/1001 and 24000/1001
  timestamps exactly within existing documented rounding tolerance.
- Trimmed live startup content never creates an overlapping, backward, or
  gapped DirectShow presentation/media timeline.
- No added queue, frame copy, worker-thread hop, capture-callback wait, or
  madVR API/OSD control input is introduced.
- Live validation logs a deterministic VP target and pre/post VP depth for
  each fresh epoch, while explicitly leaving madVR occupancy unknown.

## Out of scope

Continuous madVR queue chasing, IQualityControl emulation, scraping the madVR
OSD, reading madVR settings/rules as a control input, a global PLL, arbitrary
user-configurable delays, and changing normal steady-state delivery cadence.
