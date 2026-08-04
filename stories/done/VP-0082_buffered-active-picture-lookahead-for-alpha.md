# VP-0082: Buffered active-picture look-ahead for Alpha

## Status

Done. The Alpha implementation was merged into `v1.1.015-beta` through
[videoprocessor PR #39](https://github.com/billslack2/videoprocessor/pull/39)
as merge commit `d21d3c9` on 2026-08-04. The implementation branch was
`codex/vp-0082-buffered-lookahead`.

The merged work provides the bounded renderer-neutral decision timeline,
validated `0..8` configuration, exact Alpha queue/source identity, sparse
future-frame analysis, stale-generation rejection, and frame-associated
runtime application. Alpha adopts an accepted scheduled transition into the
same transition state that owns final crop and NLS, so those presentation
decisions change together for the effective frame. It does not add queue
depth or wait for unavailable future frames.

Live Alpha viewing with positive look-ahead found scene transitions and
active-picture detection materially improved and solid enough to close this
renderer scope. Zero remains the disabled/default behavior.

The original DirectShow/madVR goal was deliberately separated on 2026-08-04.
The merged DirectShow path validates, propagates, and retains the setting but
reports `runtime-active=0`; it does not consume scheduled decisions. That
remaining work is VP-0085 and is not part of this story's completion claim.

## User story

As an Alpha-renderer CIH user watching mixed-aspect real-world video, I want VP
to use the frames Alpha already buffers to prepare active-picture, fit, crop,
and NLS decisions before the corresponding frame is presented, so scene
changes are visually seamless without adding a second detector.

## Problem statement

VP analyzes live frames before presentation, but the current active-picture
decision is effectively consumed as soon as it becomes trusted. At a genuine
scene or geometry transition, the rendered stream can therefore expose the
detector's confirmation delay as a brief crop, pillarbox, or NLS change.

Alpha already has a bounded queue and frame/epoch timing identity. VP uses
that existing lead as practical look-ahead: analyze upcoming frames, associate
the final geometry decision with the correct frame identity, and apply crop
and NLS atomically when that frame reaches presentation. The feature never
waits for unavailable frames or increases buffering merely to satisfy a
configured look-ahead value.

## Configuration contract

Use one numeric setting rather than a Boolean plus a count:

```ini
active_picture_lookahead_frames = 0
```

- `0` disables decision look-ahead and preserves current behavior.
- A positive value requests that many frames, initially clamped to a
  documented practical maximum such as 8.
- Effective look-ahead is the smaller of the requested count and the frames
  safely available ahead of presentation.
- VP must never stall capture or presentation to reach the requested count.
- Zero and positive values must use the same detector and final-decision
  implementation; the setting changes decision timing, not geometry policy.

## Scope

1. Identify the reliable shared frame identity available from capture through
   active-picture analysis and Alpha's presentation boundary. Use the
   monotonic source sequence plus queue/source epoch as authority; retain PTS
   for diagnostics and use compact raster, viewport, and adapter generations
   only where they are required to reject stale decisions.
2. Add a frame-associated semantic decision containing the
   trusted active-picture bounds and classification, source aspect, NLS mode
   and stretch intent, confidence/state, and the earliest source frame at
   which it becomes effective. Alpha retains ownership of its explicit
   libplacebo source crop and presentation transform.
3. Let Alpha consume the scheduled decision at the matching queue/presentation
   identity and atomically update crop plus NLS transition state.
4. Analyze the safely available buffered frames without increasing queue
   depth. When fewer frames are available than configured, use the available
   lead and log the effective value.
5. On initial startup with no trusted decision, use full-raster centered fit
   with NLS waiting/native until a decision is ready. Existing queue prefill
   may make evidence available early, but this feature must not add a wait.
6. During ordinary ambiguous, dark, fade, or scene-transition evidence,
   retain the last trusted decision until the shared transition model confirms
   a replacement. Any credible content or UI outside the old bounds expands
   outward immediately for the affected frame; transient overlays never gain
   symmetric inward-crop authority.
7. Across a same-input queue flush or renderer re-prime, preserve only the
   validated source geometry needed for graceful reacquisition. Invalidate it
   for a real source, raster, pixel-format, viewport, or incompatible renderer
   generation change.
8. Apply crop and NLS atomically for the effective frame. A frame must not use
   a new crop with an old NLS mapping, or vice versa.
9. Log configured/available/effective lead, decision frame and epoch,
   application, hold, invalidation, and late or discarded decisions only on
   state changes, not per frame.

## Temporal decision rules

- Analyze each buffered frame at most once and retain only a bounded timeline.
- Preserve the detector's existing frame-rate-normalized observation cadence;
  unscheduled buffered evidence may veto or expand outward but must not become
  an extra inward-crop confirmation vote.
- A confirmed transition may be associated back to its first contradictory
  candidate only while that source frame remains unpresented and all evidence
  through confirmation is contiguous, same-epoch, and crop-trusted.
- Dark, unavailable, provisional, asymmetric, discontinuous, raster-mismatched,
  or outward-overlay evidence breaks inward-crop backdating.
- If the intended source frame has already passed, apply at the earliest
  surviving unpresented frame and log the decision as late.
- A sequence gap or drop discards pending preview decisions but retains the
  last committed geometry for safe reacquisition. A same-source re-prime does
  the same; a real source/raster/format change invalidates to full raster.
- A viewport or renderer change retains valid source geometry and rebuilds
  only the renderer-specific presentation adapter.

## Practical implementation boundary

The first increment should reuse the existing queue, active-picture detector,
transition model, frame identity, reset epochs, and final crop/NLS decision.
It is not a request for a new computer-vision detector or an unbounded queue.
If inspection shows that a reliable identity does not survive from analysis
to renderer consumption, stop at a small diagnostic/extraction increment
rather than approximating correspondence by wall-clock timing.

Bill owns the practical CIH geometry and viewing acceptance. Urvish owns the
image-analysis, dark-scene, overlay, and temporal-evidence review. Both must
approve the implementation; when a theoretically ideal design conflicts with
efficient real playback, prefer Bill's practical renderer experience while
preserving source-pixel safety.

## Validation matrix

- A/B the same Alpha content with values `0`, `2`, `4`, and `8`.
- Scope-to-scope dark cuts: no crop or NLS flash.
- Scope-to-1.85/16:9/4:3 transitions: switch at the confirmed effective frame,
  remain centered, and preserve all visible content.
- Apple TV menus, volume bars, recap buttons, and transient overlays: fit the
  actual visible bounds without blindly cancelling all zoom.
- Initial startup with insufficient history: safe pass-through, no stall, then
  deterministic acquisition.
- Configured look-ahead greater than available queue lead: use only available
  frames, with truthful telemetry and no added latency.
- Queue drop, flush, output-readiness re-prime, renderer switch, viewport
  switch, and input-format change: no stale decision crosses its generation.
- NLS on/off while paused and while playing: deterministic result for the same
  frame and geometry state.

## Required tests

- zero-look-ahead parity test against current decision behavior;
- requested-versus-available clamping without presentation stall;
- timestamped decision selection at the intended frame/PTS;
- stale epoch, format, viewport, and renderer decision rejection;
- atomic crop/NLS consumption by Alpha;
- initial unknown state and last-trusted hold behavior;
- scene transition backdating within available look-ahead;
- queue drop, flush, re-prime, and renderer-switch sequences;
- synthetic dark scope, asymmetric overlay, 16:9, 4:3, and mixed-aspect clips;
- clean x64 Release build and full native test suite.

## Acceptance criteria

- `active_picture_lookahead_frames = 0` preserves current geometry behavior
  through the same core implementation path.
- Positive values use no more than the safely available buffered lead and
  never stall or silently enlarge the live queue.
- Alpha consumes the source-frame-associated geometry/NLS decision with the
  corresponding rendered frame, without exposing an old-crop/new-NLS or
  new-crop/old-NLS mismatch.
- Initial missing state is safe and graceful; ordinary uncertainty retains the
  last valid presentation unless a real invalidating boundary occurs.
- Real-video A/B testing demonstrates a material reduction in scene-change
  crop/NLS flashes without increased false crops, lost content, or unstable
  geometry.
- Bill and Urvish both approve the implementation and validation evidence.

## Related stories

- VP-0034: Restart-free mixed-aspect NLS.
- VP-0035: Robust low-latency active-aspect transitions.
- VP-0040: Trusted active-picture detection and stable NLS engagement.
- VP-0066: Live-output pipeline, queue, identity, and epoch architecture.
- VP-0080: Fail-safe Alpha active-picture crop authority and shared geometry.
- VP-0081: Preserve madVR NLS geometry through output-readiness re-primes.
- VP-0085: Frame-correlated madVR NLS look-ahead.
