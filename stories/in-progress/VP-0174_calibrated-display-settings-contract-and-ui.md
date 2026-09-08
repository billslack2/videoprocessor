# VP-0174: Calibrated display settings contract and Color/Output UI

## Status

In progress (2026-09-08). Implementation on `codex/vp-0174-config-ui` from
current beta `38e7508f`, in `E:\codex\videoprocessor\vp-0174-config-ui`.

Originally backlog (2026-09-07). Created from an operator-facing settings guide and the
request to make the renderer's calibrated-display choices explicit, stable,
and easier to find. The guide is useful input, not verified product behavior;
its pipeline claims and all current setting semantics must be checked against
the current beta source and live output before changing defaults or help.

## User story

As a VP Renderer operator configuring a calibrated SDR display, I want one
coherent Color/Output settings area with explicit, documented display-target
choices and safe defaults, so I can configure a predictable pass-through SDR
and HDR tone-mapping path without hidden automatic decisions.

## Product contract

As clarified in the approved Config UI review, processing-preset inheritance,
source metadata following, and presentation preference may remain automatic.
For calibrated display choices, user-visible
color and output controls must resolve to an explicit effective value. An
absent setting may receive a documented default, but an `Auto` UI option must
not silently select a different color/output contract from one presentation or
source to the next. If an existing automatic behavior is retained, the UI,
effective-settings summary, configuration reference, and tests must describe
its precise trigger and result.

The baseline default calibrated-display profile is:

| Setting | Default effective value |
| --- | --- |
| Display primaries | Rec.709 |
| SDR input transfer | Gamma 2.2 |
| Display transfer | Gamma 2.2 |
| RGB output range | Full |
| Limited transport transfer | 2.2 beta, inactive while output is Full |
| Target nits (HDR tone mapping) | 100 |
| Target black (HDR tone mapping) | 0 |
| Dither target depth | 10-bit, clamped to output surface |
| Dithering | Use quality preset |
| Limited + pure Gamma 2.2 flag | Derived: On only for Limited + 2.2 |

The implementation must preserve an operator's existing explicit values during
configuration migration. It must not overwrite calibration, profile overrides,
or deployed configuration merely to apply these defaults.

## Scope

1. Audit every Color and Output control, its configuration key, current
   default, `Auto` behavior, inheritance, renderer effective value, and its
   actual place in the SDR/HDR, gamut, transfer, range, LUT, and display
   signaling pipeline. Record claims from the guide as verified, corrected, or
   unsupported before publishing them.
2. Merge the Config UI's Color and Output subsections into one clearly ordered
   calibrated-display area. Preserve compatible configuration keys and profile
   inheritance; provide a migration/compatibility plan for saved UI state and
   help links.
3. Replace non-tone-mapping automatic choices with explicit controls and the
   documented effective defaults where code and validation support doing so.
   Decide each retained exception explicitly; do not treat vague `Auto` labels
   as a product contract.
4. Present the baseline Rec.709 / Gamma 2.2 / Full-range defaults and a small,
   verified calibration matrix for Rec.709, P3-D65, and BT.2020 display modes,
   Gamma 2.2/2.4, and Full/Limited output. Clearly distinguish display-target
   settings from source metadata.
5. Make constrained settings observable: source-transfer handling, output
   range and limited-transport transfer, bit depth, BT.2020 signaling, LUT
   target-slot selection/activation/rejection, target white/black validation,
   presentation preference, and experiment gates. A rejected/ignored value
   must state its retained effective value and reason.
6. Update the public configuration and in-app help to explain fullscreen versus
   preview limitations only where verified; never present unverified preview or
   driver behavior as guaranteed product behavior.

## Non-goals

- Changing the calibrated display's picture mode, graphics-driver output range,
  Windows HDR state, or other external display configuration.
- Changing tone-mapping mathematics, HDR source adaptation, LUT transform
  order, or display-mode selection except where a documented effective-setting
  correction requires a separately approved story.
- Removing advanced controls that have an established, testable use case;
  they may remain explicit and be grouped as advanced/diagnostic options.

## Acceptance criteria

- Color and Output are presented as one discoverable calibrated-display area;
  existing explicit configuration and profile inheritance retain the same
  effective values after upgrade.
- A fresh configuration resolves to the stated Rec.709, Gamma 2.2, Full-range,
  and experiment-off defaults. The effective-settings summary reports these
  values without an ambiguous `Auto` label.
- Every non-tone-mapping automatic color/output behavior has either been
  removed in favor of an explicit value or has a documented trigger, effective
  result, and automated coverage.
- Focused source/pipeline tests prove the published SDR transfer, range,
  limited-transport signaling, gamut-target, and LUT-slot behavior. Any guide
  claim that fails verification is corrected in the shipped help.
- Invalid white/black/bit-depth and LUT configuration reports the rejected
  input, retained effective value, and reason. It never silently substitutes a
  default.
- Fullscreen calibrated-output validation and preview-path behavior are covered
  by appropriately scoped tests or explicitly labeled limitations. x64 Release
  application, Config UI, and relevant renderer tests pass.

## Dependencies and readiness

- VP-0166 owns the current target-frame LUT lifecycle and calibration boundary;
  retain its target-slot and transactional-reload contract.
- VP-0173 owns the `sdr_target_nits` classification and rejection defects;
  this story may document the interim restriction but must not independently
  redefine luminance behavior.
- Before implementation, perform a configuration-model and renderer-pipeline
  readiness review on the current beta integration base. Capture representative
  effective-settings logs and fullscreen readbacks for the baseline default,
  Limited Gamma 2.2/2.4, P3/BT.2020 targets, HDR input, and rejected settings.

## Approved implementation clarifications (2026-09-08)

- Config UI only; DeckLink configuration-only keys excluded. No deployment requested.
- Combine Color and Output tab, retaining independent profile families and inheritance.
- Remove new Auto choices for display gamma, range, Limited transfer, SDR handling,
  dither depth and numeric black. Preserve legacy/omitted behavior in existing files.
- SDR input defaults 2.2; retain explicitly named Follow source metadata.
- Retain all explicit gamma choices in the ordinary selector. Presentation labels
  become Prefer flip (allow fallback), Flip model, Legacy BitBlt.
- Keep processing/scaling/dithering preset inheritance and DirectShow Auto.
- Limited2.2 is included now for beta feedback. Editing range/transfer synchronizes
  and saves its flag; diagnostic presets cannot contradict the derived selection.
- Target nits retains its familiar label and defaults 100 for fresh profiles;
  target black defaults zero. VP-0173 is merged and owns HDR-only luminance behavior.
- Correct SDR Off help: it currently follows carrier reinterpretation, not a full
  calibrated-target bypass. No tone-mapping mathematics change is authorized here.
- Show live requested/effective state and unavailable/rejected/fallback status;
  do not claim measured Windows/HDMI behavior. Preserve missing LUT references.
- Readiness review completed with an independent specialist agent; proceed with
  the above corrections. Physical display coverage is beta feedback, not a gate
  withholding the agreed Limited2.2 choice.
