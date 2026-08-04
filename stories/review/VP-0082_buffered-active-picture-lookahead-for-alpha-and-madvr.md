# VP-0082: Buffered active-picture look-ahead for Alpha and madVR

## Status

Review. The implementation was merged into `v1.1.015-beta` through
[videoprocessor PR #39](https://github.com/billslack2/videoprocessor/pull/39)
as merge commit `d21d3c9` on 2026-08-04. The implementation branch
`codex/vp-0082-buffered-lookahead` began from integration commit `4fa0b6a`;
the remaining work is acceptance review with real Alpha and madVR content.

The first increment remains disabled by default and must support direct A/B
testing with real watched content. Initial work is tracing the existing raw,
converted, and renderer queues to prove the frame/epoch identity available to
both Alpha and DirectShow/madVR before choosing the smallest implementation.

Foundation commit `de1346f` adds the bounded renderer-neutral decision
timeline, the validated `0..8` startup setting, renderer plumbing, public
documentation, and safety/parity tests. Positive settings are deliberately
retained but runtime-inactive at this checkpoint, so this is not yet an A/B
build. The x64 Release solution builds and the native suite passes 543/543.

Bill's practical review, Urvish's detector/cadence review, and renderer
engineering approved this non-runtime foundation after inward backdating was
restricted to outward-safe expansion, stale/duplicate identities were
rejected, and the watermark was changed from presented to VP-consumed. Their
approval does not cover runtime behavior, deployment, merge, or human
acceptance.

The next increment is exact Alpha queue integration: track every accepted,
dequeued, dropped, reset, and discontinuous identity; inspect only existing
safe future frames; preserve the current `ShouldAnalyze` cadence at value 0;
and consume crop plus NLS atomically. DirectShow/madVR will first correlate
the proposed source-frame decision with actual delivery/graph-application
boundaries and remain diagnostic-only until shader-latching behavior is proven.

Alpha diagnostic integration commit `238aad4` completes the queue-ownership
portion of that work. It tracks enqueue, dequeue, overflow, resize, backlog
recovery, cadence repeat, clear, discontinuity, and generation reset; analyzes
only the existing safe future lead outside the queue lock; and attaches/logs
proposed and consumed decision identities. Sparse native analysis now covers
packed RGB, UYVY/HDYC, and v210, avoiding duplicate full-frame conversion.
Positive values remain `runtime-apply=0`: they exercise diagnostics but do not
yet control pixels. The x64 Release solution builds and 547/547 native tests
pass. Next is the separately reviewed preview/presentation state split needed
to consume the final crop and NLS decision atomically without allowing future
evidence to mutate the current frame.

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
   active-picture analysis and each renderer's presentation boundary. Use the
   monotonic source sequence plus queue/source epoch as authority; retain PTS
   for diagnostics and use compact raster, viewport, and adapter generations
   only where they are required to reject stale decisions.
2. Add a renderer-neutral frame-associated semantic decision containing the
   trusted active-picture bounds and classification, source aspect, NLS mode
   and stretch intent, confidence/state, and the earliest source frame at
   which it becomes effective. Do not share a literal renderer transform:
   Alpha owns its explicit source crop and madVR owns native bar removal.
3. Let Alpha and DirectShow/madVR consume the same decision. Renderer-specific
   code may translate it into libplacebo crop/transform or madVR DAR/shader
   controls, but may not independently choose geometry or NLS activation.
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
9. Add compact steady-state OSD telemetry such as `AP LA 4/8`. Log decision
   frame/PTS and epoch, presentation lead, hold reason, invalidation, and late
   or discarded decisions only on state changes, not per frame.

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
- Alpha and madVR consume the same source-frame-associated semantic
  geometry/NLS decision. Alpha applies it with the corresponding rendered
  frame; madVR applies it at the nearest safe delivery/graph boundary without
  exposing an old-crop/new-NLS or new-crop/old-NLS mismatch.
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
