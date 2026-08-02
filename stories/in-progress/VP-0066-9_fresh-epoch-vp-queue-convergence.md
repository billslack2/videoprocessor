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

The first real-Epson 59.94-Hz run exposed a separate allocator-state defect.
The initial renderer epoch observed a 432-ms `Deliver()` stall and converged
26 to 2; the output-readiness re-prime then completed with a 234-ms covered
interval and converged 14 to 2. After later transient capture gaps, however,
recycled `IMediaSample` instances retained `SetDiscontinuity(TRUE)`. The
retained trace contained 4,096/4,096 delivery records marked as source
discontinuities, with 4,086 at converted depth one, while the passive madVR
OSD showed only 1--2 of 8 in its decoder/upload/render queues. This is not
valid one-shot discontinuity behavior and must be corrected before using the
run to judge the Epson readiness timing.

The rejected hard-cap experiment removed already timestamped samples without
rebasing their timeline, producing VP drops and madVR repeats. The correct
solution must preserve a continuous output timeline while stale live content
is skipped; it must never create a timestamp hole merely to report a lower OSD
queue number.

The 2026-07-31 same-rate Apple TV menu-to-channel run isolated the remaining
downstream-prime failure. At 59.94 Hz, a retained transient-invalid episode
was followed by a seven-frame source-counter gap and then a one-frame gap.
VP correctly preserved the graph and normalized each discontinuity, but the
VP queue fell to one and madVR's passive OSD queues did not refill. Because
source and delivery then remained equal-rate, no surplus existed to restore
the opaque downstream reserve. A manual full graph reset restored current-
epoch preroll in 344 ms and converged VP from 13 to 2. HDR succeeded because
its EOTF transition already owned a full renderer rebuild. This proves the
missing recovery is specific to a material same-contract source gap, not the
normal convergence controller.

The 2026-08-01 asymmetric madVR-queue run isolated the reset-lifecycle defect.
With madVR configured for a 16-frame CPU/decoder queue and 8-frame GPU upload
and render queues, a fresh renderer reconstruction filled and played normally.
An in-place VP graph reset then produced exactly 16 successful synchronous
`Deliver()` calls; the seventeenth blocked for more than 1.2 seconds while
capture/conversion continued and both VP queues filled. The passive madVR OSD
simultaneously showed decoder `16/16`, upload `8/8`, render `8/8`, and present
only `1/3`. Thus the 16 accepted frames are madVR's configured decoder
admission capacity, not a VP target. The subsequent 19--28-frame source-counter
gaps occurred only after downstream backpressure and were consequences of the
stall. Repeating graph resets therefore amplified the incident.

The lifecycle audit found that VP's in-place reset retained the madVR filter
instance and executed `Stop -> source flush/NewSegment -> Run`; the segment was
published while downstream filters were stopped. DirectShow activation is now
explicitly `Stop -> Pause -> source BeginFlush/EndFlush/NewSegment -> Run`,
with transition HRESULT, graph state, reference-clock time, and segment results
logged. A failed fresh epoch may escalate once to a true renderer/filter
recreation; it cannot cycle additional in-place or source-gap resets.

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
- Once a synchronous block and three recovered deliveries prove that queued
  live pictures are stale, convergence activates a VP-owned steady latest-wins
  mode for that epoch. Preserve and rapidly process queued raw work, reduce the
  converted queue to the configured reserve, and retain only the newest
  converted work at that high-water thereafter. The delivery sequencer remains
  the sole owner of final timestamps, so skipped pictures do not create a
  presentation timeline hole. The configured value is a VP converted-queue
  bound, not a target for madVR's private queues.
- If no block is observed within three seconds, recovery is not completed
  within two seconds after the block, the target changes in the same epoch,
  raw depth cannot be observed, or display-cadence scene mode is active, fail
  closed: do not catch up and record the reason.
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
- A newly received frame after an isolated source-counter gap proves that
  capture resumed. Re-baseline cadence measurement, mark one DirectShow source
  discontinuity, and preserve the running graph. When a same-contract forward
  gap represents at least 100 ms of missing source time, request exactly one
  serialized full graph re-prime because equal-rate delivery cannot restore
  drained opaque downstream buffering. Use the exact source rational: six
  missing frames at 60000/1001 and three at 24000/1001. Counter resets remain
  local because they can race an owner-controlled EOTF/rate/format rebuild.
- After any graph reset, suppress material-gap recovery until a full second of
  consecutive current-epoch source intervals with recent downstream delivery
  and VP queues below capacity has been observed. Capture gaps created by a
  blocked downstream `Deliver()` are local backpressure evidence, not HDMI
  evidence. This recovery must use the existing nonblocking reset
  latch/coordinator and must never control DirectShow from the capture callback.

## Testable increments

1. **Policy (implemented):** `LiveEpochConvergenceController` is a pure C++14
   value state machine. It is disabled for automatic policy, requires an
   observed startup `Deliver()` block followed by three recovered deliveries,
   and requests at most one stale live-backlog convergence per epoch. It
   re-arms only for a new epoch. Native tests replay the measured 59.94-Hz
   startup trace (109.8/124.785-ms stalls followed by
   17.897/16.518/16.515-ms recovery), prove the 13-to-2 convergence, prove that
   23.976-Hz startup is classified correctly, and cover timeout, asymmetric
   raw/converted backlog, scene-mode, target-change, idempotence, and
   fresh-epoch behavior.
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
3. **Prime-to-steady integration (implemented; awaiting live validation):** the
   DirectShow delivery thread observes each delivery outcome and once per
   fresh epoch may activate converted latest-wins mode and trim converted work
   to the explicit `[queue]` target after proven block/recovery. Raw work is
   preserved and drains rapidly under that cap. `S_FALSE` is treated as
   downstream rejection: the sequencer does not commit and delivery remains
   latched until a new epoch/flush. Optional deferred-repeat failure abandons
   the clone without a renderer reset. A dedicated `*-convergence.csv`
   preserves startup proof across later periodic exports, and the manifest
   records the policy, thresholds, converted pre/post/discard depths,
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
6. **Recycled-sample discontinuity normalization (implemented; awaiting live
   validation):** source formatting and final DirectShow delivery now set both
   `TRUE` and `FALSE` explicitly on every allocator-owned sample. The
   epoch-owned `VideoFrame` flag is the sole source-gap authority; a recycled
   sample's previous flag is never imported into `ProcessedFrame`. Delivery
   telemetry identifies `epoch-start`, `source-gap`, or their combination.
   The regression test marks a sample, reuses the same object for a continuous
   frame, and proves that the second preparation clears the flag. Source
   commit `b3ea4d8` passes the clean x64 Release suite 355/355.
7. **Material same-rate gap recovery (implemented; awaiting live validation):**
   `LiveSourceGapRecoveryPolicy` converts 100 ms into a frame threshold using
   the exact input rational, keeps smaller gaps and counter resets local, and
   publishes one low-priority `source-gap-recovery` graph request through the
   existing output-pin latch. Display/HDR transitions supersede it. Every
   graph reset requires one full second of healthy source intervals before the
   policy re-arms, preventing handshake-driven reset loops. Source commit
   `ce53e9e` passed a clean x64 Release rebuild and 362/362 native tests. The
   deployed executable SHA-256 is
   `18EF03082BC7865B9E7384F0502089E142FC47157EAF2D31E27BD62FC3396135`;
   the active configuration was unchanged.
8. **Lifecycle-aware asymmetric-queue recovery (implemented; live validated
   2026-08-01):** an in-place DirectShow reset now activates the graph into
   Pause before publishing the source flush and new segment, then requests Run.
   Source-gap re-arm requires downstream health, so the known capture gaps
   caused by a blocked seventeenth delivery cannot start a reset loop. After
   one graph recovery, a fresh epoch that again proves a blocked delivery plus
   advancing input and full VP queues requests one renderer/filter recreation
   rather than another in-place reset. Source commit `d4dbf94` passed a clean
   x64 Release rebuild and 364/364 native tests. The paired deployment hashes
   are `372CDBBDF26F8366C8EA8537967A9627BD7BC5D878E8145350590A7531751AD3`
   for `VideoProcessor.exe` and
   `F002103F2EA55CD8D1302B75164915BE4D4D8758EDE33FC68E58B378D9143C20`
   for `VideoProcessorVPRenderer.dll`; configuration was unchanged.

   Live acceptance used asymmetric madVR queues (CPU/decoder 16, GPU
   upload/render 8). Actual content remained at the configured two-frame VP
   reserve while madVR remained full through app changes, screen changes,
   refresh changes, madVR recreation, and manual VP reset. The log proves the
   expected lifecycle order and successful segment HRESULTs. Where the retained
   madVR instance again stopped after its initial admission, VP requested one
   renderer recreation; later epochs recorded `graph recovery proved healthy`
   after roughly 2.0--2.9 seconds instead of alternating capacity/source-gap
   resets.

   Two retained 23.976-Hz HDR metric runs provide long-run queue evidence. One
   covered 827.9 seconds with raw depth always zero and converted depth exactly
   two for 827/828 samples, never above two. The other covered 1,231.3 seconds
   with raw depth always zero, converted depth two for 1,228/1,231 samples, and
   a maximum of three. Their manifests report zero dropped trace records,
   19,948 and 30,513 rational timing comparisons/applies respectively, zero
   timing mismatches, measured display rates 23.976429/23.976404 Hz, and a
   23.976432-Hz delivery rate. This passes the asymmetric-queue recovery and
   deterministic VP-reserve acceptance boundary without claiming observable
   madVR occupancy.

   Asymmetric-backpressure follow-up commit `b900a22` corrects the remaining
   reset-loop trigger found with madVR CPU/GPU queues set to 6/12. A full or
   high-water VP queue is now passive backpressure evidence only; it never
   requests a reset while current-epoch downstream deliveries continue to
   succeed. Recovery requires capture input to remain active while delivery
   makes no progress for the configured sustained-stall interval (minimum
   three seconds). A failed in-place recovery may recreate the renderer only
   once for the current capture-state sequence, and that fresh renderer is
   adopted as the output-readiness prime instead of stacking another graph
   reset. The transition shield reveal is one-shot, explicit zero-frame
   readiness is immediately satisfiable, and DirectShow latency telemetry now
   normalizes the graph clock into each queue epoch while retaining the raw
   clock value in CSV diagnostics. The clean x64 Release suite passes 374/374
   tests. Paired deployment hashes are
   `7CE9954738445A18223BD70F7FE2544BE14ED21CF37BCC5F5F123067C6C342C4`
   for `VideoProcessor.exe` and
   `F4DCD39DC4027DB9AD93AD9E6A239553B10CF5CFEB833ECBB9BF65B035FBD8AB`
   for `VideoProcessorVPRenderer.dll`; active configuration remained unchanged.
   Live validation with asymmetric queues remains pending.

   Live follow-up exposed the exact remaining convergence failure with a
   configured reserve of one: after output readiness created a fresh epoch,
   the uneven downstream queues left VP at raw 29 plus converted 32. The
   controller had already observed a 672-ms synchronous ingress block and
   three recovered deliveries, but its former raw-zero precondition could
   never become true in an equal-rate live pipeline. The 61 retained VP frames
   produced the observed 1.2-second VP-internal latency and held DeckLink
   buffers long enough to create 38 capture misses. Commit `7429b54` removed
   that impossible precondition, reset latency telemetry to a short non-gating
   warm-up after the cut, and separated the overlapping main-dialog groups.
   Live validation then proved its raw-plus-converted trim was still incorrect:
   it reached `0/1/1` once, but the terminal one-shot controller allowed the
   fast converter to rebuild and hold converted depth 21--22. The resulting
   roughly 364-ms VP-internal and 805-ms VP-to-scheduled values were truthful
   measurements of that regression.

   Follow-up commit `919819a` converts the terminal trim into a per-epoch
   prime-to-steady transition. Initial VP elasticity and madVR priming are
   unchanged. After proven block/recovery, a pure `LiveSteadyQueuePolicy`
   continuously removes the oldest undelivered converted work above the
   configured high-water, while preserving raw work and final delivery-owned
   timestamps. A literal zero setting uses a one-sample handoff with no retained
   reserve. Existing trace fields record each steady pre-depth, post-depth, and
   discard; the manifest identifies latest-wins mode and its high-water. The
   exact target-one/depth-22 regression is covered; the clean x64 Release suite
   passes 376/376 tests. Paired deployment hashes are
   `9C497FA0487A92FA8E123E5F8D2833CFA80A6C13903BD26D98F13B84D8E34614`
   for `VideoProcessor.exe` and
   `B4C44FCEC8C45B2994AF0F5336D04F8C0054D441804DE7CFE07F6AFB6BF86B74`
   for `VideoProcessorVPRenderer.dll`; active configuration remained unchanged.

9. **Literal zero-frame steady target (implemented; awaiting live validation):**
   omission of `[queue] steady_reserve_frames` remains the automatic policy,
   while an explicitly configured `0` now means a literal zero-frame steady
   target. The configured/omitted distinction is retained across asynchronous
   graph construction, fresh renderer creation, output-readiness publication,
   delivery reserve selection, and one-shot epoch convergence. Startup zero
   remains automatic. A native regression proves that an explicit zero target
   remains enabled after observed downstream block/recovery and authorizes a
   converted-queue trim to zero. The same increment removes the frame-offset
   edit getter's text rewrite so periodic stats reads no longer reset the
   user's caret. Source commit `bcd6845` passed a clean x64 Release rebuild and
   365/365 native tests. The paired deployment hashes are
   `8C6F672C6AFE334EED0C45ACDAEDE02F0B2F77B37FBFDFC46EC28B73FB83928B`
   for `VideoProcessor.exe` and
   `2B7EEC302837418D3B463C619A85B1F8D299263BB8FFF2114390D1EB6A9402F4`
   for `VideoProcessorVPRenderer.dll`; active configuration was unchanged.
   Live validation remains pending.
10. **Truthful VP-owned latency boundaries (implemented; awaiting live
    validation):** the old `VP Lat` and `DS Lat` values were both derived from
    an offset-adjusted capture timestamp and did not describe downstream or
    display latency. The UI and OSD now report `VP Internal`, the remaining
    `DS Lead`, and `VP->Scheduled`. `VP Internal` begins when VP accepts the
    capture frame and ends at its DirectShow delivery attempt. `DS Lead` is
    that same sample's requested presentation start minus current graph stream
    time; `VP->Scheduled` is their sum. It ends at the requested DirectShow
    presentation time and makes no claim about madVR presentation, scanout, or
    physical-display latency. Capture hardware latency is explicitly shown as
    unavailable because DeckLink does not expose the HDMI-input-to-arrival
    interval.

    The monotonic VP boundary is supported for every timestamp method. Full
    and start-only DirectShow timestamp modes also publish the scheduled
    boundary; the `None` method publishes only `VP Internal` and displays the
    other fields as unavailable without retaining a stale value from another
    mode. For Rational-Rational, the offset line states that the legacy frame
    offset is not used for RR presentation timestamps. Source commits
    `35e7301` and `a242426` passed a clean x64 Release rebuild and 368/368
    native tests. The paired deployment hashes are
    `C02D5E94024168CBF80D0D3B3ECBEC541AED05C45825EDF01B73A171A2D904F7`
    for `VideoProcessor.exe` and
    `11B753CEE92E88CCD6C63A9341F4FB038A7482D426124AFFC2B77F14415A3487`
    for `VideoProcessorVPRenderer.dll`; active configuration was unchanged.

    Follow-up commit `719aee3` prevents fresh-epoch preroll and graph-clock
    transients from appearing as stable UI values. It ignores the first one
    second of metric evidence, averages the following one second, and then
    publishes a time-based one-second smoothed value. This gate affects only
    telemetry; it never delays capture, delivery, or first picture. Every raw
    delivery observation and the displayed stable observation are recorded in
    the convergence CSV with graph stream time, VP-internal microseconds,
    DirectShow lead, VP-to-scheduled total, scheduled-known state, and display
    readiness. The debug log records one `VP LATENCY METRIC READY` marker per
    fresh epoch. The clean x64 Release suite passes 370/370 tests. Deployment
    hashes are
    `A24097490B00479AE40DE3BC03B8E48A7CD8050D0BED73D2DA2F5780BBAFA4A5`
    for `VideoProcessor.exe` and
    `B81D06E1658833541AB33180075646FC09CF012020A15326094A8346D5F83D18`
    for `VideoProcessorVPRenderer.dll`; configuration remained unchanged.

    Layout follow-up commit `557bf4a` gives the main dialog three aligned
    latency rows (`VP`, `DS lead`, and `Scheduled`), compacts the OSD labels,
    and corrects the OSD height calculation from the previous two-row budget
    to the actual three rows. It passed the clean 370/370 x64 Release suite.
    Deployment hashes are
    `D941BA08AD6194C613BAA88DB9C7EE7BA0DD5151395B52CEA3CCA6D64B471B5C`
    for `VideoProcessor.exe` and
    `BCF420C4DC37B39D1DDDCADAF04AC518D39E16E54F7E337D2E046E03701277C2`
    for `VideoProcessorVPRenderer.dll`; configuration remained unchanged.

11. **Adopt an already-proven current-graph prime (implemented; awaiting live
    validation):** the output-readiness observer no longer unconditionally
    stacks another graph reset after the current DirectShow epoch has already
    completed the exact VP-owned prime-to-steady transaction. Adoption is
    deliberately fail-closed and does not add a wait or grace period. It
    requires the same queue epoch and renderer/display generation, a recovered
    hard `Deliver()` block, converted depth at full VP capacity before the
    convergence trim, the exact configured converted target afterward, raw
    depth no greater than one, three subsequent successful deliveries, no
    delivery currently in flight, and no pending reset, retarget, retirement,
    recreation, or renderer transition. Any missing or stale evidence uses the
    existing graph re-prime immediately. A reset request rejected by the
    coordinator is re-armed instead of being lost.

    The liveness snapshot now records the epoch-owned convergence proof,
    maximum successful delivery time, and VP-retained DeckLink-buffer count,
    high-water, and oldest age. These are VP ownership diagnostics, not madVR
    queue measurements. No raw queue cap, new drop policy, worker, or frame
    copy was introduced. Three independent DirectShow/capture/regression
    reviews found no remaining release blocker after stale-generation and
    lifecycle-boundary corrections. Source commit `6971164` passed a clean x64
    Release rebuild and 405/405 native tests. Paired deployment hashes are
    `67F92A111B1E490DE6E6F82A8F895C323C1F2E9DB4F10B76290AE440C6F43DD7`
    for `VideoProcessor.exe` and
    `FAFCEA446BCECD2B017EB1A54A8BBD00419936A934C1B402D3792AF29D850BBA`
    for `VideoProcessorVPRenderer.dll`; active configuration was unchanged.

## Acceptance criteria

- Unit tests cover startup prime, startup stalls, normal delivery, target
  already met, automatic policy, one-shot behavior, fail-closed boundaries,
  timestamp/media ownership, rearm on a new epoch, rate-aware material-gap
  recovery, reset-request latching, transition priority, and healthy re-arm.
  The current native suite passes 405/405 tests in x64 Release.
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
  reset. A forward gap below 100 ms or a counter reset is logged with
  `graph_reset=0`, PPM rebaseline, and continuous DirectShow presentation/media
  timestamps. A forward gap at or above 100 ms publishes at most one
  `source-gap-recovery` graph reset and the fresh epoch converges normally.
- Exactly one delivered sample carries a source-gap discontinuity for each
  detected gap. A later ordinary sample, including reuse of the same allocator
  object, is explicitly continuous; the flag cannot become sticky across the
  remaining epoch.

## Controlled validation process (tonight)

### 2026-08-01 Epson slow-handshake correction

The first Epson run on source commit `6971164` proved that validated DXGI
cadence is not proof that the physical HDMI sink or madVR's opaque stages have
finished settling. The initial graph reached VP converted capacity (`32/32`),
retained 6-28 raw frames, and recovered from a 0.64-1.00-second `Deliver()`
stall, yet madVR remained visibly underfilled. A later manual graph reset
filled madVR. The automatic reset approximately four seconds after graph start
could still run too early after renderer restart.

The next test build therefore keeps first video and capture delivery open, but
waits a deterministic two seconds after the first continuously validated DXGI
readiness observation before issuing the existing one-shot normalization
reset. Graph loss, evidence loss/rate mismatch, or a new transition generation
restarts the settle window. A graph reset completed during the window satisfies
readiness and cancels the extra request; coordinator rejection retries without
another delay. Current-graph adoption eligibility is latched only at readiness
entry and revalidated at the settle deadline; a later proof cannot suppress
the committed reset, while later negative evidence can veto adoption. A
successful delivery stall of at least 16 expected frame periods, capped at
500 ms wall-clock, is classified as handshake-scale and cannot qualify a
current graph for adoption.

Telemetry must report `reason=awaiting-post-ready-settle`, `settle=<elapsed>/2000ms`,
and the validation tick before exactly one `output-readiness` reset request.
This is an event-relative settle with live video visible, not a first-picture
blackout or a claim that madVR occupancy is observable.

1. Begin with the known 59.94-Hz SDR monitor and the existing active
   `[queue] steady_reserve_frames: 2`. Do not replace the active configuration.
2. Start VP or rebuild the graph and observe at least 90 seconds. First
   picture remains prompt; convergence is not allowed to hold video for a
   fixed five- or ten-second readiness delay.
3. For each fresh epoch, expect at most one convergence decision:
   - if a real startup block and recovery are observed, telemetry records the
     state transition and one planned catch-up with exact converted pre-depth,
     post-depth, and discarded counts, followed by steady latest-wins discard
     records whenever production would exceed the configured high-water;
   - if the evidence is absent or unsafe, a `ConvergenceState` record with a
     reason such as `block-observation-timed-out`, `raw-depth-unknown`, or
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
   - A later sub-100-ms gap or counter reset must show
     `action=continue-local`, `ppm=rebaseline`, and `graph_reset=0`.
   - A duration-qualified gap must show `material_threshold`,
     `action=request-graph-reprime`, and one accepted
     `reason=source-gap-recovery` graph reset. Further gaps remain suppressed
     until a full healthy second; no reset loop is acceptable.
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
8. On the Epson 59.94 retest, reproduce the Apple TV menu-to-channel switch.
   If the source gap is six frames or more, expect one covered
   `source-gap-recovery` reset, a fresh epoch, VP convergence to two, and
   passive madVR queues refilled without a manual reset. A one-frame gap must
   remain local. Confirm no repeated recovery during the one-second re-arm
   window and no sustained VP/madVR drop or repeat growth.
9. Retain the asymmetric madVR CPU/GPU fixture (CPU 16, GPU 8). After stable
   startup, invoke one manual VP reset. Expect the lifecycle log order
   `stopped`, `paused-before-segment`, segment reset, and `run-requested`. The
   graph must either prove healthy delivery for two seconds or request exactly
   one full renderer recreation. It must never alternate capacity and
   source-gap resets, and VP must return to the configured two-frame converted
   reserve while madVR's passive decoder/upload/render queues continue to
   retire frames.
10. Repeat with the observed uneven CPU/GPU fixture (CPU 6, GPU 12) and the
    desired VP reserve. A VP queue at high water or capacity while delivery
    successes remain recent is healthy backpressure and must not request any
    autonomous reset. If delivery truly stops while capture continues, wait
    for the configured sustained-stall evidence, perform at most one in-place
    recovery and at most one renderer recreation for that capture-state
    sequence, and never stack an output-readiness reset on the recreation.
11. Reproduce the exact failed reserve-one epoch. It may transiently fill while
    downstream ingress is blocked, but after three recovered deliveries expect
    one `VP-0066-9 QUEUE CONVERGENCE` record reporting the raw backlog preserved
    and converted depth reduced to one. Raw should drain rapidly through the
    converter while every later conversion records `queue_depth_after <= 1`.
    OSD R/C/T should settle near `0/1/1`; it must not return to `0/21/21`.
    Latency should show unavailable briefly before republishing the caught-up
    path. Priming may include a bounded transient repeat burst, but neither
    repeats nor queue depth may grow during steady playback. The telemetry
    warm-up does not delay video.

### 2026-08-01 configurable fresh-epoch launch reservoir

FS/window retarget and source-gap traces proved that the automatic reset was
already occurring, but the historical five-frame release could not refill an
opaque downstream pipeline once capture and presentation returned to the same
rate. The next testable increment therefore changes only fresh epochs:

- latch a converted prime at the configured VP queue capacity, bounded by the
  allocator count negotiated by DirectShow with one sample of headroom;
- retain a three-frame, latest-wins raw bridge (clamped for smaller VP queues)
  so delivery can continue refilling the converted burst until downstream
  `Receive()` backpressure is observed;
- start the 2.5-second fail-open timeout at the first accepted fresh frame, not
  graph activation, so a slow Epson HDMI handshake cannot consume it;
- serialize prime completion against Reset and require current-epoch converted
  and raw depths, preventing stale work from satisfying a replacement epoch;
- keep at most three raw DeckLink-backed frames during the prime epoch and use
  the existing convergence controller to trim converted depth to the configured
  one- or two-frame steady reserve.

The published madVR `IMadVRSettings` and `IMadVRInfo` interfaces are now sampled
once per graph connection/reset. Telemetry records effective active-profile CPU,
GPU, pre-render/backbuffer, presentation-thread and flush settings plus madVR's
detected refresh, post-deinterlace rate, display mode, HDR/exclusive state, and
OSD latency. These are diagnostic configuration/timing observations only. The
published interface exposes no decoder/upload/render/present occupancy values,
so synchronous `Receive()` backpressure remains the only downstream fill
evidence used by VP.

The x64 Release solution builds and the complete unit suite passes 430/430,
including physical allocator bounds, old-epoch release rejection, small queue
clamping, bounded raw latest-wins behavior with a pre-existing backlog, and
current-epoch raw-depth proof.

## Out of scope

Continuous madVR queue chasing, IQualityControl emulation, scraping the madVR
OSD, using madVR settings/rules as live occupancy or a hard control cap, a
global PLL, arbitrary user-configurable delays, and changing normal
steady-state delivery cadence.
