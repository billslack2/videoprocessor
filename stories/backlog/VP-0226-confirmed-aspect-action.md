# VP-0226: Trigger actions on confirmed active-picture aspect changes

## Status

Backlog

Requested 2026-11-08. Next: review the current beta's confirmed aspect and action paths, then implement and validate the event. No implementation or deployment is included in this story record.

## Request

Allow a configured Action to run when the confirmed aspect ratio of the active picture changes, and let its condition and command arguments use the confirmed ratio. A width or height trigger describes the input raster and does not cover a movie changing aspect within the same HDMI signal.

## Design requirements

- Reuse the existing detector/accepted-aspect state used by VP where it provides the required confirmation. Trace its provenance and lifecycle first: the detected physical picture ratio and the ratio used to present the picture can differ during a hold, fallback, crop, subtitle preservation, or fitting. Publish the confirmed **detected active-picture** ratio, derived from source pixels, not the screen ratio, an input-raster default, a standard-ratio guess, or a presentation adjustment.
- Add an action event, proposed spelling `source.aspect_ratio.changed`, and numeric current and previous aspect variables for action conditions and argument expansion. Specify their exact names, precision, units (width / height), comparisons, and examples in the configuration reference and Config Actions UI. Values must support nonstandard measured ratios and useful range conditions. Preserve existing action configuration behavior.
- Dispatch once when the first valid confirmed ratio becomes available and once per subsequent meaningful confirmed change. Define and document how a first event represents the unavailable previous value, including what conditions and expansions do; never invent a previous ratio. Avoid repeated actions for unchanged evidence or insignificant measurement jitter; use the existing confirmation/change policy if appropriate, otherwise specify and test a bounded policy without forcing ratios to standard values. Include a practical conditional action example.
- Provisional, unknown, stale, failed, and out-of-order evidence cannot trigger a change or manufacture an absence or native-raster ratio. Temporary uncertainty does not itself emit an event; a later confirmed value is evaluated against the last valid confirmed value for the same source context. Reset source/context identity correctly on input or renderer transitions so stale results cannot fire for a new source. Document startup and reacquisition behavior.
- Work independently of stats overlay visibility and presentation settings, including Off mode. Route notifications through the existing asynchronous action infrastructure with its renderer selection, debounce/coalescing and lifecycle rules. Do not put command execution, waits, or expensive analysis on the live video path; retain current-frame identity, normal presentation cadence and bounded latency.

## Acceptance and validation

- Config validation and Actions UI accept and explain the event and supported variables; invalid contexts are rejected clearly. A real example can select a ratio range and pass its measured value to an action. Existing actions and dimension triggers continue to work.
- Automated tests cover first confirmation, a stable unchanged ratio, a confirmed change with unchanged input dimensions, small jitter, nonstandard ratios, conditional and argument evaluation, unavailable previous value, provisional/unknown evidence, recovery, delayed/out-of-order results, source and renderer resets, and duplicate/debounce behavior. Verify no event results solely from screen/profile/zoom/fill/subtitle or fallback changes.
- Exercise the actual detector-to-action path with the overlay hidden and visible, including full-raster and encoded-bar content, picture transitions, subtitles and UI overlays, and Off-mode framing. Verify events carry the intended current source ratio and are neither premature nor repeated. Record limitations of any unavailable live HDMI testing.
- Verify actions and pending analysis do not interrupt live frame advancement: measure source/presentation gaps and the mandatory critical path against the incoming frame budget, not just renderer drop counters. Confirm picture and captions remain preserved, and latency/backlog stay bounded.

## Context

On the beta inspected during initial investigation, `DetectedPictureAspect` distinguishes detected ratio, provisional status, and in-use presentation ratio. Existing aspect OSD polling is conditional on overlay visibility, so that UI path alone cannot deliver a reliable action event. These observations are starting points, not a prescribed implementation; re-check them against the current beta before coding.
