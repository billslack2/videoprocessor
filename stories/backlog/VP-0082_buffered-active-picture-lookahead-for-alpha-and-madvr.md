# VP-0082: Buffered active-picture look-ahead for Alpha and madVR

## Status

Backlog. Proposed after VP-0080 live Alpha/madVR validation on 2026-08-03.
No implementation branch has been created. The first increment must remain
disabled by default and support direct A/B testing with real watched content.

## User story

As a CIH user watching mixed-aspect real-world video, I want VP to use the
frames it already buffers to prepare active-picture, fit, crop, and NLS
decisions before the corresponding frame is presented, so scene changes are
visually seamless without adding a fragile renderer-specific detector.

## Problem statement

VP analyzes live frames before presentation, but the current active-picture
decision is effectively consumed as soon as it becomes trusted. At a genuine
scene or geometry transition, the rendered stream can therefore expose the
detector's confirmation delay as a brief crop, pillarbox, or NLS change.

The live-output pipeline already has a bounded queue and frame/epoch timing
identity. VP should evaluate whether that existing lead can become practical
look-ahead: analyze upcoming frames, associate the final geometry decision
with the correct frame identity, and apply crop and NLS atomically when that
frame reaches presentation. The feature must never wait for unavailable
frames or increase buffering merely to satisfy a configured look-ahead value.

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
   active-picture analysis and each renderer's presentation boundary. Include
   frame number, PTS, live-output epoch, input-format generation, viewport
   generation, and renderer generation as required to reject stale decisions.
2. Add a renderer-neutral timestamped final-geometry decision containing the
   trusted active-picture rectangle, presentation fit/crop transform, NLS
   mapping input, confidence/state, and the frame at which it becomes
   effective.
3. Let Alpha and DirectShow/madVR consume the same decision. Renderer-specific
   code may translate it into libplacebo crop/transform or madVR DAR/shader
   controls, but may not independently choose geometry or NLS activation.
4. Analyze the safely available buffered frames without increasing queue
   depth. When fewer frames are available than configured, use the available
   lead and log the effective value.
5. On initial startup with no trusted decision, use safe pass-through until a
   decision is ready. This expected initial limitation is acceptable and must
   not block presentation.
6. During ordinary ambiguous, dark, fade, overlay, or scene-transition
   evidence, retain the last trusted decision until the shared transition
   model confirms a replacement.
7. Across a same-input queue flush or renderer re-prime, preserve only the
   validated source geometry needed for graceful reacquisition. Invalidate it
   for a real source, raster, pixel-format, viewport, or incompatible renderer
   generation change.
8. Apply crop and NLS atomically for the effective frame. A frame must not use
   a new crop with an old NLS mapping, or vice versa.
9. Add concise OSD/log telemetry for configured, safely available, and
   effective look-ahead; decision frame/PTS and epoch; presentation lead; hold
   reason; invalidation; and late or discarded decisions. Do not log per frame
   during steady state.

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
- Repeat the same matrix through DirectShow/madVR.
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
- atomic crop/NLS consumption by both Alpha and madVR adapters;
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
- Alpha and madVR consume the same frame-associated final geometry/NLS
  decision and cannot visibly disagree about the transition frame.
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
