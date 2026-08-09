# VP-0106: Reduce madVR NLS transition latency

## Status

In progress (2026-08-09).

## User story

As a VideoProcessor user toggling NLS with madVR, I want entry and exit to
feel near-immediate and not trigger avoidable presentation stalls, while
retaining the coherent geometry/shader contract that prevents mixed frames.

## Problem

Live telemetry after the accepted VP-0104 scope-canvas repair shows that HLSL
preflight and madVR shader installation take about 30--55 ms.  The perceived
lag instead comes from two VP mechanisms:

- a 200 ms shortcut debounce intended to absorb keyboard repeat; and
- occasional multi-second waits to acquire the buffered DirectShow delivery
  lock before applying the shader/aspect transaction.  One measured NLS exit
  waited 2411.660 ms, then coincided with a display/live-queue reset and a
  roughly 1.6 s re-prime.

## Investigation checkpoint (2026-08-09)

- A normal madVR NLS entry completed the entire coherent shader/aspect
  transaction in 35.963 ms after the fixed 200 ms hotkey debounce.
- The slow exit's actual shader removal took 3.533 ms. It waited 2411.660 ms
  first for `m_deliveryGate`, which is held around a synchronous DirectShow
  `Deliver()` into madVR. Releasing that gate early would permit a sample to
  cross with a half-applied shader/aspect contract, so it is not a safe
  optimization.
- The queue reset logged immediately afterward is explicitly
  `reason=display-transition`, not an NLS-requested renderer restart; the
  NLS transaction reports `renderer_restart=0`.
- The next safe improvement candidates are reducing the shortcut's
  repeat-protection delay and adding per-delivery diagnostics/repro data to
  determine why madVR occasionally blocks `Deliver()` for seconds. Neither
  should alter NLS geometry or remove delivery serialization.

## First implementation (2026-08-09)

- Reduced the shader shortcut settle delay from 200 ms to 75 ms. The existing
  physical-key state machine continues to consume auto-repeat and waits for a
  brief key-up/modifier ordering window, but a normal NLS toggle reaches the
  renderer 125 ms sooner.
- The x64 Release GUI build passed. This change deliberately does not claim to
  solve an in-flight madVR `Deliver()` stall.

## Scope

- Trace the delivery-lock holder and identify whether an NLS rule transition
  unnecessarily waits behind delivery or initiates a reset.
- Preserve atomic shader/aspect changes and existing renderer-restart safety.
- Add telemetry and focused coverage for any changed admission/hold policy.
- Do not change the accepted numeric viewport, scope-canvas, or NLS geometry
  behavior from VP-0104.

## Acceptance criteria

- Normal madVR NLS entry/exit does not incur an avoidable multi-second VP
  delivery-lock wait.
- No renderer restart or queue reset is requested solely because a compatible
  NLS shader rule is entered or exited.
- Shader/aspect transaction coherence and existing renderer safety tests pass.
- x64 Release build and a live madVR toggle test pass.

## Integration target

The current default beta integration branch, discovered at implementation
time.
