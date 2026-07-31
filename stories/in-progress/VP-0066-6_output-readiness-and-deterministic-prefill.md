# VP-0066-6: Output readiness and deterministic post-ready prefill

## Status

In Progress (2026-07-31). The graph-independent C++14 state machine and
DirectShow reserve gate are implemented, unit-tested, and deployed for the
next live check. Normal live video starts provisionally. Once VP observes two
seconds of credible, fresh DXGI `WaitForVBlank` evidence that also passes
raw-cadence, interval, harmonic, and Windows output-family validation, it
makes one serialized **DirectShow graph re-prime**, preserving the selecting
DXGI evidence through that intentional reset. It does **not** wait for a
24-frame VP pre-reset reservoir: live evidence showed normal delivery drains
that threshold and can leave an arbitrary high-latency VP backlog without ever
requesting the reset.

The new epoch uses a VP-owned automatic reserve (currently eight frames,
bounded by capacity) unless an explicit `[queue]` steady value is present.
`startup_preroll_frames` controls the initial converted-frame gate;
`steady_reserve_frames` selects the fresh-epoch prefill threshold and
post-prime VP delivery cushion. Both accept only whole frames 0--16, where
zero means automatic policy; neither configures or observes madVR's
independently configured queues. The steady setting is not a destructive OSD
ceiling: temporary VP elasticity is required whenever madVR's synchronous
`Deliver()` stalls, so continuous sample timestamps take priority over a
momentary exact R/C/T value.

Follow-up correction (2026-07-31): live evidence showed that a manual reset,
or a startup capacity-recovery reset, could already complete the necessary
DirectShow/madVR graph re-prime before DXGI readiness evidence arrived. The
previous controller then issued a redundant readiness reset, which added a
black flash and could race a transient invalid capture-state update into a
long renderer restart. Source commit `0aa2c78` records such a completed fresh
graph epoch against the post-reset DXGI measurement generation. When that
generation validates, it publishes the selected VP reserve into the existing
fresh graph instead of resetting again. x64 Release and 324/324 tests passed;
only `VideoProcessor.exe` was deployed, with the active configuration still
unchanged. Live confirmation remains required.

Superseded hard-cap experiment (2026-07-31): source commit `c56ea3f`
interpreted a nonzero `steady_reserve_frames` as an exact VP R/C/T ceiling,
discarding newest-leading work after startup. This was deliberately tested
only after the policy-publication correction below and was rejected by live
trace evidence: at 59.94 Hz VP accepted 957 frames but delivered only 563
(35.33 Hz), with timestamped frame-number jumps such as 1253 to 1258. The
forced discards produced the observed VP drops and madVR repeats, despite
stable source timing and matched PPM. The experiment therefore does not
establish a viable exact-total queue design. The transient invalid
capture-state defer from that commit remains valid: it waits 1.5 seconds and
keeps the renderer alive when captured frames continue advancing.

Latest wiring correction (2026-07-31): live logs proved that the explicit
`[queue]` policy had never reached a fresh `CLiveSource`: `Build()` posts graph
creation asynchronously, while the dialog published the policy immediately
after requesting Build, before the output pin existed. The renderer silently
dropped that publication, so the observed 8--12 VP R/C/T values were the
default path and not a failed evaluation of the explicit two-frame target.
Source commit `4a4096e` now retains the whole-frame policy atomically and
applies it immediately after each new live-source pin is initialized, before
the graph can run; an already-live source still receives immediate updates.
Both retained and fresh-graph application are logged. An x64 Release build
and an independent Release native test build passed, with 324/324 tests
passing. Only the Release executable was deployed to
`C:\Videoprocessor\vp\VideoProcessor.exe`; the active `[queue]` configuration
remains `startup_preroll_frames: 0` and `steady_reserve_frames: 2`. The next
live run must first verify the new `DirectShow queue policy applied to fresh
graph: startup=0 steady-target=2` log line, then assess the resulting VP
R/C/T and passive madVR OSD evidence. No conclusion about the cap is valid
without that line.

Current continuity correction (2026-07-31): source commit `b067022` removes
the hard raw-plus-converted trim and its timestamp discontinuities. An explicit
steady value now replaces the automatic eight-frame **readiness prefill** with
the configured value and retains a one-frame delivery cushion below it. For
the active `steady_reserve_frames: 2`, a fresh graph pre-fills to two frames,
then normally cycles near one--two while retaining temporary bounded VP depth
when a madVR `Deliver()` call takes longer than two frame periods. This is the
necessary distinction between a desired steady queue and an unsafe hard cap.
It is not a madVR queue controller. The committed x64 Release build and the
independent native suite passed 325/325 tests, including the new explicit
two-frame prefill test; only `C:\Videoprocessor\vp\VideoProcessor.exe` was
deployed. Live validation must now show no sustained `DirectShow steady queue
target enforced` messages, no monotonically rising VP dropped count, and a
near-60 Hz VP delivery trace while retaining passive madVR OSD evidence.

This is not a first-image or HDMI-lock gate: provisional frames may display
and may stutter before the reset. The priority is a deterministic VP queue
after the reset, not pretending that VP can observe madVR's internal queue.
The five-second quarantine plus ten-second clean current-rate evidence remains
for longer readiness diagnostics, while the 30-second recency-weighted phase
confidence remains solely for phase-sensitive correction. The estimator keeps
up to two minutes of history with a 20-second recency half-life; material
current/weighted disagreement starts a fresh measurement generation. External
graph and graph-retarget resets invalidate measurement. The intentional
output-readiness graph re-prime preserves its already validated selecting
measurement so it cannot loop.

## Parent and dependency

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

Dependencies: VP-0066-3 and VP-0066-4 must have their final golden-trace and
latency guardrail evidence. It must reuse the validated display-refresh
measurement, but is not allowed to infer madVR queue occupancy.

## Objective and scope

Give the DirectShow delivery coordinator one graph-independent, unit-tested
state machine that replaces a fixed post-start delay with a fresh-epoch,
fixed-reserve policy. The states are:

```text
OutputNotReady -> PostReadyResetPending -> Prefilling -> Steady
```

The state machine begins a new readiness observation after each renderer,
display-mode, graph, or output-target transition. It accepts readiness only
when the graph is operational and the measured renderer/display refresh has
passed fresh current-rate validation in the requested **output** refresh
family. The longer weighted phase-stability predicate remains separate. This is a
deterministic renderer-readiness gate, not proof that a projector, AVR, or
HDMI sink has physically locked.

Before evidence is available, normal live delivery remains open. After short
readiness validation, VP publishes its selected internal reserve and requests
exactly one serialized DirectShow graph re-prime. This preserves the
established madVR lifecycle that empirically fills its independently
configurable queues without VP attempting to size or observe them. The fresh
epoch created by that re-prime is the only epoch allowed to form the VP steady
floor. The policy distinguishes VP startup pre-roll, VP steady reserve, and
presentation lead; the two bounded VP frame values are optional startup
configuration, never an arbitrary delay.

## Acceptance criteria

- A pure C++14 `OutputReadinessController` has controlled tests for graph-not-
  operational, missing/stale/unstable/wrong-family refresh observations,
  transition invalidation, one-reset-only behavior, and re-entry after a new
  renderer/display transition.
- The controller accepts a supplied validated display measurement; it does not
  call madVR, scrape OSD text, use `Deliver()` duration, or claim downstream
  queue occupancy or HDMI-lock proof.
- A validated two-second credible DXGI-vblank observation requests one
  serialized DirectShow graph re-prime without waiting for a speculative VP
  pre-reset depth. VP may show provisional video before that point; it never
  waits ten or thirty seconds for a first image.
- `[queue] startup_preroll_frames` and `steady_reserve_frames` accept only
  whole-frame values 0--16. Zero retains automatic policy; explicit values are
  bounded by actual VP capacity. An explicit steady value replaces the
  automatic readiness reserve and sets VP's prefill/delivery cushion; it may
  temporarily exceed its desired R/C/T depth to preserve continuous sample
  timestamps during downstream stalls, and never represents or requests a
  madVR queue depth.
- The DirectShow pin retains the selected converted VP reserve after the
  fresh-epoch reset. Completion and queue depth come from the epoch-owned VP
  liveness snapshot, never `Deliver()` timing.
- The five-second quarantine plus ten-second clean current observation and
  the 30-second weighted phase-stability interval remain separate validation
  paths; neither blocks initial display.
- Integration adds no queue, frame copy, worker thread, polling loop, or
  capture-callback wait. The delivery coordinator is the sole state owner.
- A post-ready reset flushes all pre-ready work. Prefill counts only
  current-epoch processed frames and cannot be completed by a stale or
  pre-transition frame.
- At 60000/1001 and 24000/1001, live validation shows a repeatable VP reserve
  after output readiness. Logs record output-readiness state/reason,
  transition generation, expected and observed refresh, selected reserve,
  reset request/completion, fresh epoch, and prefill completion.
- madVR OSD/frame-grab captures may be archived as passive test evidence only;
  they are never timing-control input.

## Out of scope

Direct HDMI/projector/AVR lock detection without a validated hardware or
vendor signal; dynamic madVR queue chasing; a new PLL; additional cadence
correction; arbitrary time-based queue delay; or replacing DirectShow.
