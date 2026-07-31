# VP-0066-2: Extract a graph-independent video timing controller

## Status

Accepted (2026-07-30). VP-0066-1's self-identifying baseline is accepted for
extraction, including the 24000/1001 capture-to-monitor cadence-mismatch
fixture. Display-refresh telemetry remains an explicit optional follow-up.

Implementation checkpoint (2026-07-30): `VideoTimingController` is a
DirectShow-free, value-only component with exact-rational, clock-rational,
smart-duration, monotonic, PPM, reset/preroll, invalid-clock, and externally
owned epoch tests. `DirectShowVideoTimingAdapter` retains all existing
timestamp-mode names and applies the legacy presentation lead only after the
controller's base decision. The pin remains the sole owner of media samples,
DirectShow calls, lead-ramp source, queues, workers, and reset/flush ordering.

Real-display shadow evidence (SDR 60000/1001, 2026-07-30): 12,249
RATIONAL_RATIONAL decisions compared through automatic reset and a 196-second
steady run with zero mismatches. VP occupancy stayed in the 7--9-frame band
(mostly 8); shutdown-time `Deliver` failures are excluded from steady-state
assessment. The deployed follow-up uses the controller result only after that
same per-frame equality check, with the legacy result as fallback. Other
timestamp modes and scene-aware presentation correction remain on their
existing paths pending equivalent mode-specific replay/shadow evidence.

Active Rational/Rational evidence (SDR 60000/1001, 2026-07-30): the
controller timestamp result was applied 11,110 times, with zero comparison
mismatches through automatic reset and 177 seconds of steady operation. The
VP queue again remained in the 7--9-frame band and recorded no delivery
failure during the retained steady-state event window. The pure unit suite
also simulates four continuous hours at both 24000/1001 and 60000/1001,
verifying strictly monotonic timestamps and a bounded one-100-ns-tick
rounding-boundary difference from direct rational time.

Cadence safety: 24000/1001 capture on a monitor that cannot switch to that
rate is an incompatible cadence fixture, not a PPM or queue-control problem.
This task must not use VP queue depth, madVR occupancy, or scene correction to
attempt to force 23.976 to 60 Hz.

Final verification (2026-07-30): a fresh x64 Release build completed and the
native `VideoProcessor-Test.dll` suite passed 289/289 tests. The graph-free
controller tests replay all legacy timestamp-mode shapes, preserve their names
through the adapter, cover reset and externally supplied epoch replacement,
and include four-hour exact-rational simulations for 24000/1001 and
60000/1001. This task changes neither queues, copies, workers, buffering,
DirectShow delivery, nor the existing non-Rational/Rational runtime paths.

## Parent and dependency

Parent: [VP-0066](VP-0066_rearchitect-live-video-output-pipeline.md).

Dependency: [VP-0066-1](VP-0066-1_characterize-live-output-golden-traces.md).
VP-0066-3 may begin only after this task is accepted.

## Objective and scope

Extract `PipelineEpoch`, value-type `FrameTimingInput`, `TimingDecision`, and
`VideoTimingController`. Move the existing clock/synthetic timestamp,
smoothed-duration, rational remainder, monotonicity, PPM, queue-depth,
display-phase, and scene-aware correction logic into that controller without
behavior change. Keep the existing timestamp-mode names through a compatibility
adapter while equivalent behavior is proven.

The controller must not own queues or samples, analyze frames, call
`IMediaSample::SetTime`, or call DirectShow delivery APIs. It receives values
and returns a decision; the existing pipeline remains responsible for applying
it until VP-0066-4.

## Acceptance criteria

- Unit tests run without a DirectShow graph, renderer, capture device, worker
  thread, or real `IMediaSample`.
- Tests cover exact 24000/1001 and 60000/1001 timing, rational accumulation,
  monotonicity, positive/negative PPM, capture faster/slower than display,
  queue growth/depletion, scene-aware correction, invalid/missing timestamps,
  missing display rate, reset, and epoch replacement.
- Four-hour simulations at both exact rates keep timestamp error bounded and
  preserve the intended rational remainder behavior.
- Replaying VP-0066-1 fixtures produces equivalent timestamp and correction
  decisions within the recorded tolerances; any difference is documented and
  explicitly approved.
- The compatibility adapter preserves all existing timestamp modes and this
  extraction introduces no queue, copy, worker handoff, or buffering change.

## Out of scope

Queue/processor extraction, DirectShow sample delivery, and coordinator
lifecycle changes, which remain for VP-0066-3 and VP-0066-4.
