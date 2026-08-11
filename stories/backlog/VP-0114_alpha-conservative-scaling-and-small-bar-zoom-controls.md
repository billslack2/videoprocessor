# VP-0114: Alpha conservative scaling and small-bar zoom controls

## Status

Backlog (2026-08-10). Created from the request to provide Alpha equivalents of madVR's conservative small-resize and small-black-bar zoom behaviors. UX design is an explicit prerequisite to implementation; no UI, configuration, or rendering behavior has changed.

## User story

As an Alpha-renderer operator, I want separately configurable policies for insignificant final-size corrections and very small letterbox/pillarbox bars, so I can avoid needless resampling or, when I explicitly choose it, trade a tiny edge crop for a completely filled physical screen.

## Problem and terminology

Alpha's normal aspect-preserving **fit** retains every source pixel. When a 2.40:1 picture is fitted into a 2.35:1 physical screen, it correctly leaves a few destination lines at the top and bottom. A **fill** operation removes those bars by scaling to fill the limiting axis and cropping the opposite source edges—about 2.1% of total picture width for that example, or about 1.0% per side.

The two controls are deliberately not aliases:

1. **Suppress insignificant scaling** retains the current uncorrected size when the final correction is below its threshold. It avoids a small resample and may leave a small destination border; it must never crop.
2. **Zoom small destination bars away** applies a bounded fill operation when the normal aspect-preserving fit would leave only small bars. It removes the bars and necessarily crops the opposite source edges.

Neither option changes trusted active-picture detection, crop authority, subtitle fitting, presentation-envelope construction, physical-screen aspect, or NLS policy. Those source-space and destination-layout responsibilities remain separate.

## Proposed configuration contract

Add two independent, per-screen-profile integer settings, expressed in destination **lines per edge**:

| Setting | Range | Proposed default | `0` meaning |
| --- | --- | --- | --- |
| `suppress_small_scale_lines` | 0..50 | 0 | Disabled; apply normal final scaling. |
| `zoom_small_bars_lines` | 0..50 | 0 | Disabled; retain normal fit bars and do not crop to remove them. |

The 50-line maximum applies to each bar edge, not their combined height/width. It covers rounding and modest near-aspect mismatches (the reported 2.40:1-to-2.35:1 case is far below it) while avoiding a setting that silently treats substantial cinematic bars as "small." A 100-line maximum would allow materially larger content loss and is not recommended for the first release. Evaluate thresholds in final output-pixel/line coordinates after physical-screen and requested-viewport layout, not in source pixels or before nested physical-screen fitting.

No separate enable checkbox is proposed: a value of `0` is the unambiguous persisted off state, and a nonzero value enables that policy. The UX designer must confirm this follows the established VP configuration pattern and decide whether a switch plus a disabled value field is more comprehensible in the final editor.

If both values are nonzero, document and test the order explicitly:

1. Calculate the normal Alpha nested aspect-preserving fit.
2. Apply small-scale suppression only if its no-crop behavior qualifies.
3. Independently evaluate small-bar zoom from the normal-fit unused space.
4. If zoom qualifies, use the bounded fill/crop result; zoom takes precedence because it is the explicitly selected bar-removal policy.

Log the selected policy, configured thresholds, normal-fit rectangle, unused bars by edge, final rectangle, scale factor, and exact source-edge crop. This keeps a black-bar report distinguishable from intentional small-bar zoom.

## Scope

1. Design and implement the two persisted per-profile settings, validation, defaults, configuration reference, safe live-apply behavior, diagnostics, and editor controls.
2. Apply each policy only at Alpha's final destination-layout stage, after physical-screen/requested-viewport calculation and without changing source detector decisions.
3. Preserve current default behavior exactly when both values are zero.
4. For zoom, crop symmetrically on the axis opposite the small unused bars, subject to existing alignment requirements. If an odd pixel must be allocated, use one documented deterministic side/rounding rule.
5. Reject invalid, out-of-range, noninteger, or malformed configuration with established validation and fallback behavior; retain unknown configuration content as VP-0097 requires.
6. Keep subtitle/OSD/presentation-envelope content safe. A zoom decision may not discard a source edge that the existing presentation contract marks as required visible content. If that conflicts with zoom, retain the fit result and report why zoom was suppressed.

## UX design checkpoint

Before implementation, review the present Screen Config information architecture and comparable bounded numeric controls with the UX designer. The design must decide and record:

- Exact section and labels, distinguishing no-crop scaling suppression from fill-and-crop bar zoom.
- Whether the controls belong under Screen geometry, an advanced Geometry disclosure, or another established pattern from VP-0113.
- Whether the `0 = off` numeric-control pattern is sufficient or a separate checkbox/switch is needed.
- Visible unit wording (for example, `lines per edge`) and help text that makes zoom's crop consequence unmistakable.
- Presentation of unavailable/non-Alpha renderer values and profile inheritance/default behavior.
- An operator preview, confirmation, or diagnostic affordance if UX judges a crop-cap warning necessary.

Do not reuse madVR labels verbatim merely for familiarity. VP wording must state the observable policy and must not imply that the no-crop setting removes black bars.

## Acceptance criteria

- With both thresholds at `0`, every existing Alpha layout, source rectangle, and final fit remains unchanged.
- `suppress_small_scale_lines` accepts only 0..50 and, when it qualifies, bypasses only the insignificant final resample; it never crops source content.
- `zoom_small_bars_lines` accepts only 0..50 and, when each relevant normal-fit destination bar is within threshold and content-safety rules permit it, fills the target and crops only the opposite source edges.
- A 2.40:1 active picture on a 2.35:1 physical screen with a qualifying zoom threshold fills the screen and reports its small symmetric horizontal crop; with zoom disabled it preserves the picture and reports the expected small top/bottom destination bars.
- A bar larger than the configured per-edge threshold does not trigger zoom.
- Zoom never changes trusted crop authority or loses a required subtitle/OSD presentation envelope; a blocked zoom has an explicit diagnostic reason.
- The editor clearly conveys `0 = off`, the selected unit, and that zoom may crop picture edges, following the UX-approved layout.
- Profile persistence, validation, runtime reload/live apply, renderer switching, physical-screen fitting, requested viewports, NLS-off behavior, and Alpha diagnostics remain correct.
- Focused unit/layout tests, geometry tests for wider/narrower/equal content, and a clean x64 Release build pass. Live validation includes the reported 2.40:1-to-2.35:1 case and confirms both settings independently.

## Non-goals

- Replicating madVR's entire zoom-control feature set or its UI verbatim.
- Detecting or cropping encoded black bars as a new source-authority path.
- Altering projector zoom, lens memory, mechanical masking, or physical screen calibration.
- Introducing NLS or anamorphic stretching as a substitute for an aspect-preserving fit or explicitly bounded fill/crop decision.

## Dependencies and references

- VP-0097: safe standalone configuration editor and live configuration apply.
- VP-0098: arbitrary CIH active-picture envelope and physical-screen fit.
- VP-0099: renderer-neutral NLS geometry and safety policy.
- VP-0113: Screen Config grouping and fixed-unit field presentation.
- User-provided madVR Zoom Control screenshot and Alpha black-bar screenshot, 2026-08-10.

