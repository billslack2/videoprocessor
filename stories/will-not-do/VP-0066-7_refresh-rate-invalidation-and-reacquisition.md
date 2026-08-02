# VP-0066-7: Invalidate and reacquire refresh-rate measurements at real transition boundaries

## Status

Will Not Do (2026-08-02). The planned DXGI-centered invalidation authority was
superseded by the implemented madVR `IMadVRInfo` detected refresh-rate source,
periodic runtime telemetry, and transition/reset generation handling in
stable baseline `f4a443e`. VP retains DXGI/Windows observations as supporting
diagnostics rather than building this second readiness authority.

## Parent and dependencies

Former parent: [VP-0066](../review/VP-0066_rearchitect-live-video-output-pipeline.md).

Related child: [VP-0066-6](../review/VP-0066-6_output-readiness-and-deterministic-prefill.md).
The implementation must reuse the existing display-rate estimator and output-
readiness model rather than creating a second refresh-rate authority. The
validated queue/timing boundaries from VP-0066-3 and VP-0066-4 remain required
inputs.

## User story

As a viewer moving between renderers, refresh modes, and HDMI-synchronizing
transitions, I want VP to discard stale display-rate evidence and reacquire a
credible current rate before making a final re-prime decision, while still
showing the first valid picture immediately.

## Problem statement

A five-second aging window is useful for deciding whether an initial rate
observation is recent, but it is not enough evidence to declare the final rate
accurate after a disruptive sync. The system must separate two decisions:

1. whether it can show an initial image; and
2. whether the measured rate is ready for final timing/re-prime use.

VP already invalidates DXGI measurement on renderer start and DirectShow
display-change events, but the completed DirectShow reset boundary must also
invalidate the measurement. Every renderer restart, renderer swap, completed
DirectShow reset, and observed refresh-family change must be treated as a
potentially new measurement generation.

## Required behavior

- Keep initial image display immediate when the renderer and frame are usable.
- Mark the refresh measurement as warming, unavailable, or invalidated after a
  relevant transition; do not present an old measurement as freshly confirmed.
- Use a longer, non-configurable ten-second current evidence window before
  declaring the rate ready for final re-prime or timing decisions.
- Tie the deterministic reset/re-prime decision to successful invalidation and
  reacquisition, not to a blind elapsed-time timer.
- Invalidate after every completed DirectShow reset, including resets that keep
  the same renderer object.
- Invalidate on renderer start, renderer restart, renderer swap, display-mode
  transition, and a detected change of refresh family.
- Do not reset merely because the queue is empty, shallow, or temporarily
  changing during normal playback.
- Do not repeatedly invalidate or reset while the same transition generation
  is still being reacquired.
- Keep user refresh-rate overrides separate from measured-rate readiness; an
  override may remain authoritative while diagnostics identify the measured
  observation as unavailable.

## Transition and generation contract

Define one explicit measurement generation and invalidation reason for each
boundary. At minimum, log renderer generation and identity, reset generation
and completion, monitor/display-mode identity, nominal refresh family, previous
accepted rate and age, raw and compensated candidates, evidence-window start
and duration, readiness state/reason, and whether reset/re-prime was requested,
completed, or suppressed as a duplicate.

An observed refresh-family change must invalidate previous evidence before the
new candidate is accepted. Small fractional differences within the same
family must not cause an endless reset loop.

## Acceptance criteria

- A first usable frame can be displayed without waiting ten seconds.
- A rate cannot become ready for final re-prime decisions until it has a valid
  current ten-second evidence window and passes existing freshness, stability,
  interval, harmonic, and nominal-family checks.
- Renderer start, restart, swap, completed DirectShow reset, and actual
  refresh-family change each create exactly one invalidation generation.
- A failed or incomplete reset does not falsely mark the new measurement ready;
  successful repeated resets do not retain pre-reset evidence.
- Normal queue fluctuations and same-family fractional noise do not trigger
  repeated measurement resets.
- Reacquisition causes at most the intended deterministic re-prime, not a
  timer-driven loop.
- OSD and logs distinguish initial image availability from final rate
  readiness.
- Tests cover startup, renderer swap/restart, completed and failed reset,
  HDMI/display resync, same-family fractional change, actual-family change,
  stale evidence, zero/unstable candidates, and repeated observations of one
  transition.
- Recorded traces demonstrate a current stable rate without reintroducing
  startup image blackout or stale-rate timing.

## Out of scope

Changing the display-rate estimator’s mathematical measurement method,
introducing a new PLL, blindly resetting on a fixed timer, treating madVR’s
unreported queue state as known, or changing user-configured refresh overrides.
