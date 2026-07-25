# VP-0016: Make scene detection available for the alpha renderer

## Status

Draft. The detection signal appears to exist in shared VP logic, but the alpha
renderer reports it as unavailable. Confirm the ownership and lifecycle of the
reported result before implementation.

## User story

As an alpha-renderer user, I want scene detection and its status to work the
same way as the established renderer, so scene-aware diagnostics and future
timing correction are not falsely reported as unavailable.

## Feasibility question and initial assessment

This should be possible if the alpha renderer receives the same captured frame
metadata or pixel access used by the existing detector. “Unavailable” may be a
capability flag tied to the renderer rather than proof that detection failed.
The story must distinguish:

1. detector unavailable/not initialized;
2. detector initialized but warming/no stable result;
3. detector active with a valid result;
4. detector failed or intentionally disabled.

The alpha renderer must not claim scene detection is active merely because a
shared counter exists. Conversely, it must not report unavailable when the
shared detector has already produced valid observations.

## Scope and constraints

- Trace the shared detector from capture through `VideoProcessorDlg.cpp`, the
  source/output pins, renderer state, and OSD fields.
- Compare the alpha and established renderer contracts for timestamps, frame
  ownership, dimensions, pixel format, color metadata, and reset boundaries.
- Reuse the existing detector where its inputs and thread-safety contract are
  valid; do not create a second competing detector without evidence it is
  required.
- If alpha needs a renderer-side adapter, it must be lifecycle-safe and must
  not block frame submission or drain queues while detection warms.
- Preserve the current OSD vocabulary (`Warming`, `Active`, `Unavailable`) and
  make the reason explicit in logs.

## Implementation plan

1. Produce a state/data-flow diagram and identify the exact assignment that
   causes alpha to report unavailable.
2. Define a renderer-independent detection result with state, confidence or
   validity, frame/timestamp association, and reset generation.
3. Connect alpha to the valid shared detector or add a bounded adapter. Ensure
   startup, source changes, renderer resets, display-rate changes, and scene
   changes transition through the same state machine without reset loops.
4. Add rate-limited logs for initialization, warm-up, valid observations,
   invalid observations, reset generation, and failure reason. OSD must report
   `Unavailable` only when the capability/result is genuinely unavailable.
5. Keep scene correction disabled unless the existing correction policy says it
   is enabled; this story makes detection truthful and observable first.

## Verification and acceptance

- Alpha reports `Warming` during initialization, `Active` after a valid stable
  result, and `Unavailable` only for a documented unavailable/failed state.
- Detection values match the established renderer for identical input and are
  associated with the correct frame/timestamp generation.
- SDR, HDR, LLDV-style metadata, 23.976/24 Hz, and 59.94/60 Hz are exercised.
- Renderer restart, source transition, and detector reset do not starve queues,
  drop frames solely because detection is warming, or loop resets.
- A test proves that a valid detector result cannot be hidden by an alpha-only
  capability flag.
- Logs identify whether the issue is missing input, not warmed, invalid,
  disabled, failed, or merely not supported by the current alpha path.

## Dependencies

Review VP-0005/VP-0006 and VP-0013 before implementation because detector
warm-up and reset behavior must not recreate the queue starvation/reset problems
those stories track.
