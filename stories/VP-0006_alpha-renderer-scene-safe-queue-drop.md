# VP-0006: Alpha renderer optional scene-safe queue-drop correction

## Status

Planned. Implement only after VP-0005 provides baseline evidence that ordinary
queue-pressure drops or accumulated cadence drift occur in realistic playback.

## Context

The alpha libplacebo renderer does not use DirectShow `IMediaSample` start/stop
timestamps. It receives `VideoFrame` objects in `OnVideoFrame`, stores them in a
bounded FIFO, and `RenderLoop` renders the next queued frame through libplacebo.
When full, it currently discards the oldest frame immediately:

`LibplaceboVideoRenderer.cpp`, `OnVideoFrame` queue-limit loop.

That keeps latency bounded but may create a visible skip during motion. The
DirectShow renderer already has mature scene-aware correction concepts, but the
alpha renderer's `SetSceneAwareTimingCorrection(bool)` is currently a no-op and
does not implement `SetSceneTimingRates`, `SetSceneTimingReadiness`, or
`SetSceneTimingPhase`.

## User story

As a user who enables the existing Scene Aware/Advanced UI option, I want the
alpha renderer to honor the same on/off choice and prefer a visually safe scene
boundary when it must drop a frame, without changing default alpha behavior when
the option is off.

## Non-goals

- Do not alter the DirectShow renderer or its established correction behavior.
- Do not add DirectShow start/stop timestamp modes to alpha.
- Do not add repeats in this story; they are VP-0007.
- Do not rewrite the alpha render thread or introduce a global shared timing
  subsystem as part of this proof of concept.

## Implementation plan

1. Wire `SetSceneAwareTimingCorrection(bool)` to an atomic enabled state in
   `LibplaceboVideoRenderer`, defaulting to disabled. The current UI already
   calls this renderer-interface method; alpha should use the exact same user
   switch with no new setting.
2. Add a small alpha-local scene-boundary analyzer. Reuse/copy only proven,
   renderer-independent image-analysis math from the DirectShow scene detector;
   do not refactor the production DirectShow detector during this story. Analyze
   an available luma representation after frame formatting or another safe
   alpha-native point that does not retain invalid source buffers.
3. When disabled, retain the exact current FIFO behavior: immediately drop oldest
   frame at the configured queue limit.
4. When enabled and a drop becomes necessary, mark a bounded pending correction
   rather than immediately discarding. Select an eligible queued frame at a safe
   detected boundary, then drop one frame and release its source buffer exactly
   once.
5. Set a strict bounded deferral deadline/queue safety limit. If no safe boundary
   appears before the deadline, perform the current ordinary drop and log a
   `scene-safe deadline fallback`; latency must never grow without bound.
6. Implement the currently ignored timing-rate/readiness inputs only to the
   degree needed to determine that a correction is pending. Keep phase control
   out of scope unless VP-0008 proves it is required.
7. Add renderer metrics required for the existing OSD scene status: enabled,
   warming/active/unavailable state, pending correction, safe drops, and fallback
   drops. If an alpha metric cannot be truthfully supported, report
   `Unavailable`, not a misleading zero or `Ready`.
8. Log enable/disable transitions, detector readiness, correction cause,
   selected boundary, and deadline fallback. Rate-limit normal status lines.

## Verification

- With the UI feature off, compare logs and queue/drop behavior to a baseline;
  no scene analysis or deferred selection occurs.
- With it on, induce bounded queue pressure and verify a detected safe boundary
  is selected when available.
- Test no-cut footage: the deadline fallback occurs, queue latency stays bounded,
  and each source buffer is released once.
- Test cut-heavy material: normal corrections are logged as scene-safe.
- Test stream reset, renderer restart, hidden/minimized window, refresh switch,
  and GPU-render failure. Pending state must clear safely.

## Acceptance criteria

- The existing UI toggle has identical meaning for the alpha renderer.
- Disabled behavior is materially unchanged from the current alpha baseline.
- Any alpha queue-pressure drop is categorized in logs as ordinary, scene-safe,
  or deadline fallback.
- No DirectShow code change is required for the alpha PoC.
