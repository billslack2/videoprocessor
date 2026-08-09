# VP-0106: Reduce madVR NLS transition latency

## Status

Done (2026-08-09).

## Completion checkpoint (2026-08-09)

- Source commits `1adb64f` (windowed madVR NLS scope canvas) and `63ff434`
  (75 ms shader shortcut debounce) were pushed on
  `origin/codex/vp-0106-madvr-nls-transition-latency`.
- Merge commit `25f6203` is the verified remote tip of
  `origin/v1.2.001-beta` and contains both source commits.
- The focused scope-placement regression and x64 Release GUI build passed.
- The x64 Release executable and renderer DLL were deployed together to
  `C:\Videoprocessor\vp`, hash-verified, and successfully launched through
  the existing `start.bat` command. Active configuration was not changed.
- The previous deployed pair is retained under
  `C:\Videoprocessor\vp\backups\vp0106-shortcut-debounce-20260809-135913`.

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

## Delivered scope

- madVR now receives a centered windowed scope canvas while NLS is requested,
  aligning its windowed presentation with the already-correct fullscreen and
  Alpha behavior.
- NLS/shader shortcuts reach the renderer after a 75 ms settle period rather
  than 200 ms, while physical auto-repeat remains guarded.
- Multi-second delivery stalls are confirmed to be in-flight madVR
  `Deliver()` waits, not shader compilation or an NLS-triggered reset. Further
  changes require renderer-side stall reproduction and are intentionally out
  of this completed safe responsiveness increment.

## Integration target

The current default beta integration branch, discovered at implementation
time.
