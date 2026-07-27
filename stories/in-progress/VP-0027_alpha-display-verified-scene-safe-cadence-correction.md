# VP-0027: Alpha display-verified scene-safe cadence correction

## Status

In Progress. The fail-closed Alpha-native correction policy and action plumbing
are implemented on `codex/vp-0025-scene-detection` at source commit `967df5b`.
Release x64 builds and all 77 unit tests pass. Live correction behavior,
independent presentation-trace verification, and Epson visual validation remain
deferred to the combined VP-0024/25/27 validation run.

Supersedes VP-0006 and VP-0007.

## User story

As an Alpha-renderer user with closely tuned capture and display clocks, I want
the rare unavoidable frame drop or repeat to occur once at a detected scene
boundary, based on observed display debt and verified afterward, so long-term
playback remains low-latency and visually smooth.

## Correction contract

Prediction, authorization, selection, action, and verification are separate:

1. Capture/display rate difference predicts when a correction window is near.
2. VP-0026 queue state requests a correction direction.
3. VP-0025 identifies a safe scene event on a specific source sequence.
4. VP-0024 observed queue/present debt authorizes exactly one action.
5. Alpha performs one native drop or repeat.
6. Subsequent DXGI presentation evidence verifies that debt changed by one
   frame and no unintended glitch occurred.

No action may be authorized by a rate estimate, queue depth, scene event, or
single late frame alone.

## Required design and implementation

1. Reuse the established DirectShow policy concepts where renderer-independent:
   compatible-rate gating, warm-up, fractional phase/debt, correction window,
   event IDs, cooldown, one-action accounting, and bounded fallback.
2. Replace DirectShow timestamp, `IMediaSample`, quality-notification, media-time,
   delivery-gap, and clone mechanics with Alpha-native actions.
3. For capture-faster/display-slower debt, omit and release exactly one selected
   queued source frame. Prefer the correction placement proven by VP-0025:
   final old-scene frame when lookahead is available, otherwise explicitly
   validate dropping the first new-scene frame.
4. For capture-slower/display-faster debt, retain a known-good frame or GPU
   result under an explicit ownership model and present it for exactly one
   additional refresh. Do not assume a flip-discard backbuffer can be reused.
5. Keep at most one pending action per generation and one consecutive cadence
   repeat. Clear pending/retained state on stop, reset, renderer failure, source
   discontinuity, swapchain/mode change, or generation replacement.
6. Open the scene window from prediction, but authorize only when measured
   source-to-present debt and elastic-buffer direction agree. Invalid or
   disjoint DXGI evidence suspends corrections.
7. Add a bounded deadline/no-cut fallback selected from the maximum queue age
   contract. Categorize scene-safe, deadline-fallback, hard-overflow, starvation,
   render-failure, and presentation-glitch events separately.
8. After an action, correlate the source sequence, present ID, refresh count,
   phase/debt before and after, queue age, scene event, and timing generation.
   A failed or ambiguous verification must not trigger an immediate second
   action.
9. Keep the existing user-facing scene-aware enable/disable choice. Disabled
   behavior must match the accepted VP-0026 baseline.

## Verification

- Use controlled rate offsets in both directions and confirm one correction per
  accumulated frame, not per timing sample.
- Validate cut-heavy, long-take, fade, flash, near-black, and static material.
- Confirm scene-safe placement visually and through source/present correlation.
- Confirm exact source-buffer ownership with no leak, double release, stale
  repeat, or repeat loop.
- Test 23.976/24 and 59.94/60 on the Epson for sustained sessions long enough
  to exercise real or safely accelerated corrections.
- Exercise resets, mode changes, renderer switching, display profiles,
  minimized/restore, GPU failure/reconstruction, and measurement disjoint.
- Compare latency and presentation-glitch counts with the accepted VP-0026
  baseline.

## Acceptance criteria

- Every correction has one measured cause, one scene event or bounded fallback,
  one native action, and one verification result.
- Scene-safe corrections reduce measured debt by exactly one frame.
- Corrections stop while timing evidence is warming, invalid, disjoint, or
  generation-stale.
- No reset loop, unbounded queue growth, stale-frame loop, double release,
  increased steady-state correction rate, or unexplained latency regression.
- DirectShow/madVR behavior remains unchanged.

## Dependencies

Depends on accepted VP-0024, VP-0025, and VP-0026.

## Implementation progress

- A renderer-independent Alpha policy separates stable DXGI evidence,
  compatible measured rates, accumulated fractional phase, elastic-queue
  direction, presentation debt, scene selection, action, cooldown, and
  verification.
- Corrections fail closed while disabled, warming, unavailable, disjoint,
  rate-incompatible, generation-stale, cooling down, or awaiting verification.
- A scene can authorize an action only after phase, queue direction, and
  measured presentation debt agree. The bounded no-cut fallback uses the
  maximum queue-age window.
- A native drop consumes and releases the selected current boundary source
  frame before upload. It is counted separately from hard overflow and render
  failure.
- A native repeat preserves the queue-owned source reference and requeues it at
  the front for exactly one additional ordinary render. It never assumes a
  flip-discard backbuffer is reusable and cannot form a repeat loop.
- Queue and detector/source generations are folded into one policy epoch.
  Resets, source changes, mode changes, and replacement generations therefore
  discard phase, pending action, retained ownership, and verification state.
- The following observed presentation must change debt by exactly one.
  Unchanged or multi-frame results are ambiguous, logged, and cooled down
  rather than retried immediately.
- Alpha detector, correction counters, prediction status, compatibility, and
  last-action data are now forwarded through the optional-plugin proxy to the
  existing renderer UI.
- Seven policy tests cover invalid evidence, incompatible rates, full drop and
  repeat gating, queue disagreement, generation replacement, and exact
  one-frame verification. Together with the added telemetry gap test, the
  complete suite passes 77/77.

Remaining work is live validation with controlled offsets, scene-heavy and
long-take material, reset/mode/minimize recovery, independent DXGI tracing, and
Epson 23.976/24 plus 59.94/60 sessions. In particular, confirmed hard cuts are
reported one frame back by the detector, so the current-frame placement must be
confirmed visually before acceptance.
