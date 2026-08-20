# VP-0136: Prevent transient same-axis inward aspect switches

## Status

Done (2026-08-20). Same-axis inward crop candidates now require eight seconds
of continuous confirmation. The logged seven-second 2.20:1-to-2.35:1 false
transition remains on the stable geometry, while outward recovery retains its
existing adjacent-confirmation latency.

## User story

As a scope-screen operator watching stable 2.20:1 material, I want transient
volume UI, dark rows, and short matte-like observations to leave the trusted
picture geometry unchanged, so the image does not jump to 2.35:1 and back
after an overlay appears or disappears.

## Confirmed live failure

The deployed Alpha trace in `C:\Videoprocessor\vp\logs\vp.log` records the
complete transition:

- Before 00:48:53, trusted bounds were `0,208-3840,1952`, aspect `2.2018`.
- At 00:48:53, four adjacent observations proposed
  `0,260-3840,1896`, aspect `2.3472`. The transition model logged
  `materially different geometry candidate`, followed by
  `trusted transition confirmed`.
- Alpha immediately applied the new crop. The final layout widened from
  `picture=121.1..3718.9` to `picture=2.3..3837.7` on the configured
  2.35:1 viewport.
- The upper-edge subtitle/volume path did not engage: bar telemetry remained
  `top=0 overlay_top=0`, so no upper-edge presentation placement protected or
  identified the preceding receiver volume UI.
- At 00:49:00, a presentation envelope found live pixels outside the temporary
  crop on both vertical edges (`edges=-T-B`). The detector reacquired
  `0,208-3840,1952`, aspect `2.2018`, and restored the prior layout.

The incorrect 2.3472 geometry was active for approximately seven seconds.
This is an actual logical active-picture transition, not subtitle translation
or an anamorphic/NLS shader change.

## Root cause

`ActivePictureTransitionModel::IsNestedOrthogonalCrop()` recognizes a nested
crop only when the candidate introduces a newly trusted crop axis:

```cpp
(candidateAxes & stableAxes) == stableAxes &&
(candidateAxes & ~stableAxes) != 0
```

A 2.20:1-to-2.35:1 transition moves the top and bottom inward while retaining
the same `TOP_BOTTOM` trusted axis. It therefore returns false, bypasses
`NESTED_CROP_CONFIRMATION_SECONDS`, and commits after the ordinary adjacent
confirmation count. The existing four-second nested dwell also would not
fully suppress this seven-second trace even after the predicate is corrected.

## Required behavior

1. Classify any strict containment of an established crop as an inward nested
   crop when the candidate retains authority for every axis it changes. This
   includes deeper top/bottom cropping on an existing `TOP_BOTTOM` axis and
   deeper left/right cropping on an existing `LEFT_RIGHT` axis.
2. Do not publish the observed 2.2018-to-2.3472 candidate during the complete
   seven-second live sequence. Retain the established logical geometry and
   final layout throughout.
3. Use a dedicated same-axis inward confirmation duration of at least eight
   seconds, or an evidence rule proven against the captured trace to provide
   equivalent protection without relying on the unrecognized top-overlay
   classification.
4. Keep outward recovery visibility-first. Returning from a deeper crop to a
   recently trusted containing geometry must remain immediate or use only the
   existing adjacent-confirmation latency, so live pixels are not hidden.
5. Preserve eventual mixed-aspect support. A genuine, continuously affirmed
   same-axis inward aspect change that outlives the documented dwell must
   eventually publish once; it must not oscillate or restart its timer on
   equivalent quantized bounds.
6. Reset pending inward evidence on contradictory geometry, return to the
   stable bounds, source/raster generation changes, or loss of required crop
   authority. Confirmations must not straddle those boundaries or scene cuts.
7. Log the candidate kind, stable and candidate bounds/aspects, elapsed and
   required dwell, rejection/reset reason, and final commit. Diagnostics must
   distinguish same-axis inward dwell from orthogonal/windowbox dwell.
8. Do not require the subtitle/volume classifier to recognize the overlay in
   order for active-picture geometry to remain safe. Overlay classification
   may improve presentation, but it is not crop authority.

## Acceptance criteria

1. A deterministic replay establishes `0,208-3840,1952`, supplies
   `0,260-3840,1896` with trusted top/bottom bar evidence for seven seconds at
   23.976 Hz, and returns to the original bounds without any publication or
   final-layout change.
2. The same replay passes at 24, 25, 29.97, 30, 50, 59.94, and 60 Hz using
   elapsed-time semantics rather than a fixed frame count.
3. A continuously stable 2.3472 candidate beyond the configured/documented
   dwell eventually commits exactly once.
4. A deeper same-axis crop lasting less than the dwell, including candidates
   beginning or ending on a scene boundary, never replaces the stable crop.
5. A four-sided/windowbox candidate retains its existing conservative
   behavior, and adding a new trusted axis remains covered.
6. A return from 2.3472 to recently trusted 2.2018 geometry remains prompt and
   never hides pixels for the inward-crop dwell duration.
7. Provisional or overlay-like observations without affirmative crop authority
   cannot accumulate toward the inward commit.
8. Focused transition-model, active-picture, source-crop, and captured-trace
   tests pass, followed by the complete x64 Release test suite.

## Regression matrix

- 2.20 stable -> seven-second 2.35 candidate -> 2.20 stable;
- 2.20 stable -> sustained genuine 2.35 candidate -> committed 2.35;
- 2.35 stable -> outward 2.20/full-raster candidate;
- same-axis left/right inward and outward transitions;
- orthogonal windowbox transitions;
- quantized candidates that vary within the stable geometry deadband;
- scene cut, source generation, raster generation, and unavailable evidence
  while an inward dwell is pending;
- localized top volume UI and bottom subtitle sequences, whether or not the
  vertical-overlay classifier accepts them.

## Validation and release evidence

- Implemented in VideoProcessor commit `08b27c3` and fast-forwarded to
  `origin/v1.2.001-beta` after rebasing against the latest remote branch.
- The exact logged top/bottom bounds are covered at 23.976, 24, 25, 29.97,
  30, 50, 59.94, and 60 Hz; left/right same-axis containment is also covered.
- Focused transition-model tests passed 34/34.
- The complete x64 Release solution built successfully.
- The complete native suite passed 866/866, and the standalone Config UI test
  suite passed.

## Boundaries and related work

- VP-0122 retains logical geometry through subtitle and volume overlays.
  VP-0136 closes a separate trusted-transition hole after the pixels resemble
  a deeper crop with affirmative same-axis bar authority.
- VP-0124 owns safe outward lookahead. VP-0136 must not slow outward expansion
  or fail-open behavior.
- VP-0129 owns vertical overlay arbitration and presentation translation. This
  story changes logical inward-crop authority, not subtitle drift or placement.
- Do not identify receiver UI by application, OCR, logo, or neural network.
- Do not disable mixed-aspect detection or permanently pin content to 2.20:1.
- Do not change NLS shader selection, anamorphic scaling, screen aspect, or
  source generation semantics as part of this fix.

## Likely implementation areas

- `src/VideoProcessor-Lib/ActivePictureTransitionModel.cpp`
- `src/VideoProcessor-Lib/ActivePictureTransitionModel.h`
- `src/VideoProcessor-Test/ActivePictureTransitionModelTests.cpp`
- captured active-picture/source-crop replay fixtures and diagnostics
