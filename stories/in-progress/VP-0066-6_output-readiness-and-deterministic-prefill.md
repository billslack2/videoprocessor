# VP-0066-6: Output readiness and deterministic post-ready prefill

## Status

In Progress (2026-07-30). The graph-independent C++14 state-machine model is
implemented and covered by controlled tests. It is now attached as a
UI-thread **passive observer** of the existing validated DXGI
display-refresh decision, with real-display transition logs. It deliberately
does not yet control capture, reset, or delivery, so this checkpoint does not
change live behavior. The VP-0066-3 arrival trace provides the initial
59.94-SDR guardrail: 125 ms median and 141 ms p95 capture-arrival to
`Deliver()` start, with the VP processed queue at seven frames at p95.
Actuation begins only after the observer evidence is accepted.

## Parent and dependency

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

Dependencies: VP-0066-3 and VP-0066-4 must have their final golden-trace and
latency guardrail evidence. It must reuse the validated display-refresh
measurement, but is not allowed to infer madVR queue occupancy.

## Objective and scope

Give the DirectShow delivery coordinator one graph-independent, unit-tested
state machine that prevents pre-handshake live frames from becoming a
variable-age downstream backlog. The states are:

```text
OutputNotReady -> PostReadyResetPending -> Prefilling -> Steady
```

The state machine begins a new readiness observation after each renderer,
display-mode, graph, or output-target transition. It accepts readiness only
when the graph is operational and the measured renderer/display refresh is
fresh, stable, and in the requested **output** refresh family. This is a
deterministic renderer-readiness gate, not proof that a projector, AVR, or
HDMI sink has physically locked.

While output is not ready, VP discards live capture rather than retaining a
backlog. On the first accepted readiness observation it requests exactly one
serialized reset. The new epoch created by that reset is the only epoch that
may form the post-ready prefill. VP then uses its existing effective reserve
target to obtain an exact current-epoch VP queue depth before entering steady
delivery. The policy distinguishes VP reserve from presentation lead
internally, logs their selected frame/millisecond values, and does not expose
an arbitrary user-configurable delay.

## Acceptance criteria

- A pure C++14 `OutputReadinessController` has controlled tests for graph-not-
  operational, missing/stale/unstable/wrong-family refresh observations,
  transition invalidation, one-reset-only behavior, and re-entry after a new
  renderer/display transition.
- The controller accepts a supplied validated display measurement; it does not
  call madVR, scrape OSD text, use `Deliver()` duration, or claim downstream
  queue occupancy or HDMI-lock proof.
- The passive integration records only transition/state/reason observations;
  it requests no reset and does not gate, queue, copy, delay, or deliver any
  capture frame. Its logs explicitly label a successful refresh observation as
  renderer readiness rather than physical HDMI-lock proof.
- Integration adds no queue, frame copy, worker thread, polling loop, or
  capture-callback wait. The delivery coordinator is the sole state owner;
  capture observes only its published discard/admit gate.
- A post-ready reset flushes all pre-ready work. Prefill counts only
  current-epoch processed frames and cannot be completed by a stale or
  pre-transition frame.
- At 60000/1001 and 24000/1001, live validation shows a repeatable VP reserve
  after output readiness. Logs record output-readiness state/reason,
  transition generation, expected and observed refresh, selected reserve and
  presentation lead, reset request, and prefill completion.
- madVR OSD/frame-grab captures may be archived as passive test evidence only;
  they are never timing-control input.

## Out of scope

Direct HDMI/projector/AVR lock detection without a validated hardware or
vendor signal; dynamic madVR queue chasing; a new PLL; additional cadence
correction; public arbitrary-delay configuration; or replacing DirectShow.
