# VP-0035: Robust low-latency active-aspect transitions

## Status

Backlog — separate NLS enhancement following VP-0034. The current conservative
detector behavior was intentional protection against false positives, so this
story must improve responsiveness through confidence and transition modeling,
not by indiscriminately reducing confirmation thresholds.

## User story

As a viewer of mixed-aspect movies, I want VP to recognize real Scope/IMAX
scene changes promptly enough that stale geometry is not visibly applied, while
remaining resistant to fades, black frames, subtitles, credits, logos, dark
scenes, and transient edge content.

## Current behavior

The DirectShow active-picture detector currently analyzes approximately every
sixth video frame and requires five matching analyzed candidates before
publishing a stable rectangle. At approximately 23.976 fps, those samples span
about 30 source frames, or roughly 1.2 seconds. Conditional shader refresh is
also timer-driven at approximately one-second intervals.

That conservative policy limits false activation, but it can leave the
previous stable rectangle published while a materially different candidate is
being confirmed. VP can therefore continue using stale geometry during a real
aspect transition. This delay is distinct from VP-0034's permanent loss of the
armed rule after renderer replacement.

## Scope

- Make active-picture transitions both bounded and robust for all consumers,
  with NLS mixed-aspect playback as the primary validation case.
- Preserve coherent rectangle snapshots and detector generations.
- Do not introduce renderer restarts as a response to active-aspect changes.
- Do not simply lower `ANALYSIS_INTERVAL_FRAMES` or `REQUIRED_MATCHES` without
  measured evidence that false positives remain controlled.

## Required investigation and implementation

1. Build a reproducible transition corpus containing:
   - hard Scope/IMAX cuts in both directions;
   - fades to/from black and brief all-black frames;
   - dark scenes with bright center content;
   - subtitles partially or fully in black bars;
   - credits, logos, overlays, and letterbox-edge highlights;
   - fixed-aspect sports and 4:3 controls.
2. Instrument candidate rectangle, current stable rectangle, confidence,
   matching/contradictory sample counts, transition state, publication
   generation, and decision latency.
3. Model `Stable`, `Candidate transition`, and `Unavailable/ambiguous` states.
   A materially contradictory candidate must not silently leave stale geometry
   labeled as fully current.
4. Evaluate asymmetric hysteresis:
   - require strong, spatially consistent evidence to leave a stable state;
   - allow clear symmetric bar appearance/disappearance to confirm faster than
     ambiguous edge changes;
   - require additional confirmation around fades, near-black scenes, and
     transient overlays.
5. Decouple detection publication from the one-second UI timer where practical.
   Notify or poll the renderer often enough to make mapping changes promptly
   without doing expensive full-frame analysis on every frame.
6. Define safe behavior during uncertainty. It must avoid a visibly destructive
   stale crop/stretch while also avoiding rapid toggling caused by a single
   false candidate.
7. Keep all thresholds documented and, unless field testing proves a user
   control is necessary, internal rather than adding configuration complexity.

## Performance and safety constraints

- Detection must remain a bounded, low-cost operation suitable for 4K capture.
- Do not block capture, delivery, conversion, or renderer presentation threads.
- A detector transition must not reset queues, rebuild the graph, restart the
  renderer, or change HDR state.
- Avoid oscillation when content hovers near an aspect tolerance boundary.

## Verification

Measure both transition latency and false-transition rate over the corpus.
Repeat at 23.976/24, 25, 29.97/30, and 59.94/60 fps so behavior is based on
time/confidence rather than an accidental frame-rate-dependent delay.

For each real aspect transition, record:

- first contradictory observation;
- stable publication time and frame;
- NLS mapping-change time and frame;
- number of false candidates and reversals;
- queue depth, dropped frames, and renderer lifecycle events.

Run long fixed-aspect controls and require no false NLS mode changes from
subtitles, fades, dark scenes, credits, logos, or ordinary cuts.

## Acceptance criteria

- Every sustained Scope/IMAX transition is detected and published within a
  documented bounded latency at all supported frame-rate families.
- The renderer reacts to a published generation promptly without relying on a
  one-second UI polling boundary.
- The detector does not falsely change aspect mode in the agreed transition
  corpus or long fixed-aspect control playback.
- Ambiguous frames do not cause oscillation, destructive stale mapping, or
  renderer/queue lifecycle changes.
- Logs make the detector's confidence, transition decision, latency, and
  rejection reason independently reviewable.

## Dependency

VP-0034 is the primary correctness requirement. This story may be developed
after or alongside it, but faster detection cannot substitute for durable NLS
state and restart-free runtime mapping.

