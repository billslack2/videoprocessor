# VP-0066-6: Output readiness and deterministic post-ready prefill

## Status

In Progress (2026-07-31). The graph-independent C++14 state machine and
DirectShow reserve gate are implemented, unit-tested, and now actuated. Normal
live video starts provisionally. Once VP observes two seconds of credible,
fresh DXGI `WaitForVBlank` evidence that also passes raw-cadence, interval,
harmonic, and Windows output-family validation, it waits for a measured VP
startup reservoir of `capacity - reserve` frames (24 with the current 32/8
policy). It then makes one serialized **DirectShow graph re-prime**, preserving
the selecting DXGI evidence through that intentional reset. The new epoch
retains a VP-owned reserve of eight converted frames (bounded by capacity).
This replaces the legacy five-second post-start reset with a repeatable queue
condition while retaining the empirically proven madVR re-prime lifecycle.

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
readiness validation, VP waits for its exact 24-frame startup reservoir, then
publishes its internal eight-frame reserve and requests exactly one serialized
DirectShow graph re-prime. This preserves the established madVR lifecycle that
empirically fills its independently configurable queues without VP attempting
to size or observe them. The fresh epoch created by that re-prime is the only
epoch allowed to form the eight-frame VP steady floor. The policy distinguishes
VP reserve from presentation lead and does not expose an arbitrary
user-configurable delay.

## Acceptance criteria

- A pure C++14 `OutputReadinessController` has controlled tests for graph-not-
  operational, missing/stale/unstable/wrong-family refresh observations,
  transition invalidation, one-reset-only behavior, and re-entry after a new
  renderer/display transition.
- The controller accepts a supplied validated display measurement; it does not
  call madVR, scrape OSD text, use `Deliver()` duration, or claim downstream
  queue occupancy or HDMI-lock proof.
- A validated two-second credible DXGI-vblank observation plus the exact
  VP `capacity - reserve` startup reservoir requests one serialized DirectShow
  graph re-prime. VP may show provisional video before that point; it never
  waits ten or thirty seconds for a first image.
- The DirectShow pin retains an internally selected eight-frame converted VP
  reserve (bounded by capacity) after the fresh-epoch reset. Completion and
  queue depth come from the epoch-owned VP liveness snapshot, never
  `Deliver()` timing.
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
correction; public arbitrary-delay configuration; or replacing DirectShow.
