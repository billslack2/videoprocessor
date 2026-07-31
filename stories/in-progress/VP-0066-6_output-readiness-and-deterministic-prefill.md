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

The new epoch retains a VP-owned automatic reserve (currently eight frames
when output readiness publishes it, bounded by capacity). The optional
startup-only `[queue]` policy makes the two VP concepts explicit and
testable: `startup_preroll_frames` controls the initial converted-frame gate;
`steady_reserve_frames` controls the retained DirectShow converted-frame
floor. Both accept only whole frames 0--16, where zero means automatic policy;
they never configure or observe madVR's independently configured queues.
The x64 Release build and 324/324 native tests passed, and only
`C:\Videoprocessor\vp\VideoProcessor.exe` was deployed from source commit
`1208da7`; the user's active configuration was not changed.

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
  bounded by actual VP capacity and the steady reserve takes precedence over
  the automatic readiness reserve.
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
