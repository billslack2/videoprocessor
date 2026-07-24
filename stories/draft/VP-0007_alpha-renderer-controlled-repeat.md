# VP-0007: Alpha renderer controlled repeat correction

## Status

Draft follow-up to VP-0006. Do not implement until VP-0005 confirms a
capture-slower-than-display case that needs correction and VP-0006 has proven
safe queue/drop ownership.

## Context

The alpha renderer currently dequeues one input frame and presents it. It has no
intentional repeat behavior. A repeat is not a duplicate source-buffer reference:
it must re-present a known-good retained render result or safely re-render a
retained frame without double-releasing its source buffer.

Unlike a DirectShow renderer, alpha cannot solve this by changing sample
start/stop timestamps. It needs a renderer-native, bounded decision in the
render/presentation pipeline.

## User story

As an alpha-renderer user, I want rare capture-slower-than-display drift to be
corrected by a deliberate, observable repeat rather than accidental starvation,
while preserving bounded latency and avoiding repeated-frame loops.

## Non-goals

- No interpolation or motion-compensated frame generation.
- No indefinite repeat of a stale image when capture stops.
- No behavior change while scene-aware correction is disabled.
- No refactor of the DirectShow timing path.

## Implementation plan

1. Define, from VP-0005 data, the exact condition that constitutes a repeat
   requirement. It must be based on persistent cadence/queue evidence, not a
   single late capture timestamp.
2. Decide and document the retained-frame ownership model before coding:
   - preferred: retain a completed GPU presentation/renderable result if the
     libplacebo/DXGI path supports it safely;
   - fallback: retain one `VideoFrame` reference with explicit ownership and
     re-render it once, releasing only after it is no longer retained.
3. Implement one-frame, rate-limited repeats only when scene-aware correction is
   enabled and the timing condition is pending. Never repeat while an input frame
   is already ready to render unless the policy explicitly requires it.
4. Add a maximum consecutive-repeat count of one for cadence correction and a
   hard timeout that returns to normal playback. Capture loss/starvation remains
   a separately logged condition, not a cadence correction.
5. Integrate with VP-0006 scene status and logs: pending repeat, applied repeat,
   suppression reason, and fallback/timeout.
6. Clear retained content and pending repeat state on Stop, Reset, renderer
   failure, display switch, screen-profile change if it reconstructs resources,
   and video-state discontinuity.

## Verification

- Use controlled test input or a test seam to create a small persistent
  capture-slower-than-display mismatch.
- Confirm exactly one deliberate repeat is counted and normal input presentation
  resumes immediately.
- Confirm no source-buffer leak, double release, stale-frame loop, or elevated
  queue latency during repeated corrections.
- Exercise Stop/Start, Reset, refresh switch, fullscreen transition, and GPU
  reconstruction while a repeat is pending.

## Acceptance criteria

- Repeats are opt-in through the same existing scene-aware UI control.
- Repeats are bounded, counted, and explicitly logged.
- Normal alpha playback and disabled behavior remain unchanged.
