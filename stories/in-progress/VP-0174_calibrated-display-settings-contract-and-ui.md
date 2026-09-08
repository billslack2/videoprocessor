# VP-0174: Calibrated display settings contract and Color/Output UI

## Status

In Progress (2026-09-08). Direct Config descriptions and window-order correction.
Startup compatibility regression previously fixed and deployed.
Latest correction `4cfb56cd`; prior unified-profile implementation `2c0e0223`.
Commit `2c0e0223` on `codex/vp-0174-config-ui`, based on verified latest beta
`v1.3.005-beta` at `38e7508f`.
PR: https://github.com/billslack2/videoprocessor/pull/82 (not merged).

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

- Config UI only; DeckLink configuration-only keys excluded. Deployment subsequently
  authorized explicitly, followed by a no-LUT Gamma 2.2 architectural review.
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

## Completion and deployment evidence (2026-09-08)

- Full x64 Release solution build succeeded. Native tests: 1,102 passed.
- Final complete Config UI run: 65 passed, zero failures, process exit 0.
  A physical two-monitor test was unavailable; synthetic negative-origin clamp
  and live target-window coverage ran.
- Independent implementation review completed; affected-profile flag syncing,
  retired-Auto omission and same-value activation findings fixed.
- Deployed matching host/renderer plus Config UI and documentation to
  `C:\Videoprocessor\vp`, with hashes checked against staged Release artifacts.
- Backup: `C:\Videoprocessor\vp\backup-before-vp0174-20260908-101306`.
  Active configuration SHA256 unchanged:
  `69CDA6889972E43D80EFB2D24B01A4A9D4528041384F3E21923D4943252EE9EA`.
- No tone-mapping math changed. Physical HDMI/display response is unmeasured;
  beta feedback is accepted for the Limited 2.2 default.
- Architectural review: high confidence in internal no-LUT pure-2.2 rendering
  and single accepted-range conversion in both Full and Limited. DXGI Full G22
  means piecewise sRGB and Studio G22 means piecewise BT.709, so downstream
  interpretation remains a concrete concern. Dedicated no-LUT Full/Limited
  grayscale sweeps are a coverage gap. Full report: [architectural review](../work/vp0174-gamma22-architecture-review.md).
- Ready for PR review and user beta validation; no automatic merge performed.
- Tracker correction: the prior index still said Backlog despite the in-progress
  file. This transition updates both to Review.

## UI consistency correction (2026-09-08)

- User review: profile name, shortcuts and rule must always remain visible,
  matching other profile tabs. Removed the Color/Output-only collapse wrapper.
- Commit `0a6a12e1`, pushed to PR #82. Full x64 Release build succeeded;
  existing renderer-profile layout/persistence test passed; Windows screenshot
  verified the profile fields are visible without an expander.
- Corrected Config UI and matching Release host/renderer pair deployed and
  SHA256 verified. Active configuration unchanged.
- Backup: `C:\Videoprocessor\vp\backup-before-vp0174-profile-visible-20260908-103822`.

## Approved unified-profile follow-up

Keep Color names, rules and shortcuts; copy the first Output baseline settings into Color profiles. Archive all old Output sections with comments rather than delete them. Preserve saved gamma choices and expose LUT input transfer independently. Test migration idempotence, selection, save/reload and numerical no-LUT/LUT Full/Limited gamma ramps. Continue on PR #82 from verified beta 38e7508f.

## Unified-profile completion and deployment (2026-09-08)

- One Color / Output profile family now owns all 16 previous Color/Output keys.
  First legacy Output baseline copied to every Color profile; Color selectors
  retained. Extra Output profiles and all their settings/comments/selectors are
  archived in inactive legacy_output sections. Exact original-byte backup precedes
  the first migration save. No settings silently discarded.
- New explicit passthrough and calibration_lut_input_gamma separate SDR reference,
  physical display response and LUT input encoding. Legacy Off/Auto and existing
  gamma values retain their interpretation. Existing fresh 2.2 choices preserved.
- LUT paths/enable, dithering/depth and HDR nits/black remain Rendering-owned.
- Clean full x64 Release solution rebuild succeeded. Incremental PDB corruption
  was resolved by clean rebuild; failed incremental artifacts were not deployed.
- Native: 1,107 exercised, 1,106 passed full run; sole help-inventory failure
  corrected and passed focused rerun. Config UI: all 66 passed, exit 0.
- New GPU grayscale readbacks cover 8/10-bit Full/Limited, 2.2 unity, 2.4-to-2.2
  reference preservation and LUT conversion once; intentionally duplicated
  correction is distinguishable. Runtime migration, archive collisions, malformed
  legacy sections, rules, unknown inactive keys, backup and save idempotence tested.
- Independent specialist source review found no migration, double-gamma or LUT
  fallback blocker. Numerical rendering coverage is stronger; HDMI/optical response
  remains unmeasured, including downstream DXGI G22 interpretation concerns.
- Windows screenshot verified one profile list and always-visible identity fields.
  Deployed Config startup succeeded, exit 0. Physical two-monitor placement unavailable;
  synthetic negative-origin and live-window placement coverage ran.
- Release package verified 58 immutable files. Seven runtime/help files replaced:
  matched host/renderer pair, Config executable/discovery DLL, root and docs help,
  and VP-0174 configuration guide. All deployed hashes match staged Release artifacts.
- Backup: `C:\Videoprocessor\vp\backup-before-vp0174-unified-20260908-113649` (includes unchanged original active config).
- Active configuration edits: **none**. SHA256 remains
  `69CDA6889972E43D80EFB2D24B01A4A9D4528041384F3E21923D4943252EE9EA`.
- Full file hashes: deployment-evidence.json in that backup folder.
- Detailed follow-up architectural assessment is recorded in
  [Gamma 2.2 review](../work/vp0174-gamma22-architecture-review.md).

## Startup compatibility correction

User reported fatal unknown section legacy_output.default. The original configuration
still has vprenderer.output.Default: ConfigFile creates the archive in memory.
Renderer/Config validation accepted it but a separate host ownership check rejected
it. Share archive ownership and the host/Config ownership validation; regression-test
old and saved migrated files before a new Release deployment. No user config edit required.

### Startup correction completion

- Commit `4cfb56cd` published to PR #82. Archive ownership now shared by host
  startup and Config Save/Apply. Both original vprenderer.output and already-saved
  legacy_output sections pass; unrelated unknown sections remain rejected.
- Clean full x64 Release rebuild passed. Full native run: **1,108 passed**.
  Complete Config run: **66 passed**, exit 0.
- Actual VideoProcessor host startup tested with a byte-identical isolated copy
  of the active configuration and copied selection state. Logs confirm configuration
  acceptance, capture/rendering startup, and normal clean shutdown. Original
  configuration/state paths were not used for test writes.
- Corrected Release host/renderer pair and Config executable/discovery DLL deployed
  and SHA256 verified. Active configuration edits: **none**.
- Backup: `C:\Videoprocessor\vp\backup-before-vp0174-startup-20260908-114705`.
- Active config SHA256: `69CDA6889972E43D80EFB2D24B01A4A9D4528041384F3E21923D4943252EE9EA`.
- This corrects a real validation coverage miss in the original deployment;
  Config-only startup testing had not exercised the host ownership check.

## Direct wording and window-order follow-up

User requested direct behavioral descriptions instead of legacy labels, and
reported Config no longer remaining modal over VP. Source trace found earlier
cross-process modality removal. Clarification requested about blocking VP input;
pending that preference, the independently confirmed stacking regression is fixed:
VP's topmost fullscreen surface could cover Config although both had TOPMOST set.
A foreground event now triggers a relative-order check and nonactivating repair.
No polling or cross-process native owner is introduced. Regression reproduced
before the change and passed after it. SDR labels now describe all three behaviors;
saved config tokens retain semantics. Complete Release validation/deployment pending.
