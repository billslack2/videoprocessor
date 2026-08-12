# VP-0124: Safely accelerate outward active-picture transitions with bounded lookahead

## Status

In progress. Design and prototype work started from the default
`v1.2.001-beta` tip `b7de6a2` on 2026-08-12.

## User story

As a VP Renderer CIH user watching mixed-aspect material such as IMAX films, I
want VP to use frames already present in its queue to recognize genuine
outward picture expansion before presentation, so transitions engage sooner
without making subtitle, volume-OSD, crop, or NLS behavior less safe than the
current VP-0122 baseline.

## Problem

VP-0082 added a bounded `0..8` active-picture decision timeline, but the
setting remains disabled by default and predates VP-0122's stronger separation
of logical geometry, current-frame presentation safety, subtitle translation,
and broad opposing-band picture proof. Simply enabling the old path would not
provide sufficient renderer-level evidence that subtitle/OSD behavior is
unchanged, and its queued extraction currently runs synchronously before
analysis-cadence filtering.

Mixed Scope/IMAX material can therefore still expose a short confirmation
delay. The present behavior is otherwise very good and is the safety baseline,
not something this story is permitted to trade away.

## Safety invariants

1. Lookahead may reveal picture pixels earlier; it may never backdate or create
   an inward crop, a mixed-axis crop, subtitle translation, FIT authority, or
   NLS authority.
2. Every preview result is bound to exact transport, accepted-sequence,
   source-frame, timestamp, source-format, viewport, and renderer identity.
   Any mismatch or discontinuity discards it.
3. Current-frame evidence remains authoritative. Provisional, unavailable,
   scene-crossing, localized subtitle/OSD, changing-target, or retention-vetoed
   evidence falls back to the zero-lookahead behavior.
4. Lookahead never waits for frames, enlarges the queue, blocks presentation,
   or treats reduced availability as starvation.
5. `active_picture_lookahead_frames: 0` remains bit-for-bit equivalent to the
   current default behavior.
6. Source/profile/generation changes clear pending proof. Proof may not combine
   top and bottom occupancy from different frames.

## Configuration and UI

Add an explicit mode beside the existing frame count:

```ini
active_picture_lookahead_mode: off   # off | shadow | outward_apply
active_picture_lookahead_frames: 0   # 0..8
```

- `off`: no preview work; current behavior.
- `shadow`: analyze and report decisions, but never change presentation or
  logical geometry.
- `outward_apply`: apply only a validated, current-frame-safe outward result.
  This remains opt-in until the full acceptance gate passes.
- Legacy configurations with only a nonzero frame value migrate to `shadow`,
  never silently to applied behavior.
- VP Renderer queue `target_frames`, not DirectShow `lead_frames`, determines
  practical availability. Reliable lead `L` normally needs
  `target_frames >= L + 1` and `queue_size >= target_frames`.
- The UI should warn when requested lookahead exceeds `target_frames - 1`, but
  it must not reject the configuration: runtime availability is dynamic and
  safely clamps to `min(configured, available distinct non-repeat frames)`.
- Initial applied rollout is limited to one frame. Values 4..8 remain
  diagnostic/stress settings until performance data justifies them.

## Prototype increments

### 1. Shadow and performance foundation

- Add the explicit mode to schema, runtime configuration, live profile
  application, Config UI, examples, and validation.
- Move `ShouldAnalyze`/eligibility checks ahead of queued pixel extraction.
- Cache immutable evidence once per frame identity and retention evidence once
  per trusted-base signature; never cache source pointers.
- In shadow mode, create and validate decisions but never call
  `AdoptPublishedDecision`.
- Correct telemetry so it reports the real mode and whether application was
  possible, vetoed, late, or depth-limited.

### 2. Reveal-only certificate

- Add a separate exact-frame outward certificate rather than overloading a
  stable transition decision.
- Require same-generation base geometry, outward containment, valid raster and
  alignment, exact identity, and current-frame revalidation.
- Merge a validated certificate only toward greater current-frame reveal.
  Never mutate logical geometry, subtitle drift, or NLS state.

### 3. Confirmed outward timing

- Reuse VP-0122's three-consecutive-frame broad opposing-picture proof over
  contiguous buffered frames.
- After shadow validation, allow a confirmed outward logical transition to be
  associated with the first qualifying still-buffered frame.
- An intervening localized/ambiguous frame, target change, cut, gap, repeat,
  drop, or generation change resets proof.

## Required validation

- Differentially run lookahead 0/1/2/4/8 and compare logical geometry,
  presentation envelope, translation, final crop/layout, NLS, and visible
  pixels. The only permitted difference is an earlier proven outward reveal.
- Mixed 2.39/1.90/1.78/full IMAX transitions, one/two-frame inserts, rapid
  alternation, cuts, and within-shot expansions.
- The logged Scope to transient windowbox regression.
- Bottom, top, growing, disappearing, and two-edge subtitles; volume OSD;
  subtitle drift and hold; localized top+bottom UI that must not accumulate.
- Cuts on candidate/confirmation/effective frames, fades, near-black frames,
  queue starvation/overflow/resize, cadence repeats/gaps, and every generation
  change.
- Native RGB, UYVY, HDYC, V210, P010, and P210 evidence parity.
- Generated-buffer composition through preview detector, timeline, scheduled
  decision, current-frame retention gates, and final layout.
- 1080p/4K at 23.976, 29.97, 50, 59.94, and 60 fps. Steady-state preview p99
  must remain below 5% of a frame period with no material increase in drops,
  starvation, or render-deadline misses.

## Acceptance criteria

- Explicit Off/Shadow/Outward apply behavior is visible and round-trippable in
  Config; old files cannot silently activate applied lookahead.
- Off is behaviorally identical to VP-0122.
- Shadow performs no presentation or logical-state mutation.
- Applied mode never backdates inward/mixed transitions and never excludes a
  pixel visible under the zero-lookahead current-frame policy.
- Queue shortfall degrades immediately without waiting or adding latency.
- Renderer-level composition, format-parity, scene, subtitle/OSD, and
  performance gates pass before applied mode is recommended.
- Real Eternals-style A/B testing demonstrates a useful reduction in outward
  transition delay without a subtitle, OSD, crop, or NLS regression.

## Related stories

- VP-0082: original buffered Alpha active-picture lookahead.
- VP-0085: DirectShow/madVR lookahead.
- VP-0098: arbitrary CIH active-picture envelope and screen fit.
- VP-0110: smooth subtitle translation and drift timing.
- VP-0122: logical-geometry retention through subtitles and volume overlays.

