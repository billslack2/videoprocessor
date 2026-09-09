# VP-0174: Calibrated display settings contract and Color/Output UI

## Status

Review (2026-09-09). Complete Color / Output calibration profiles implemented,
independently reviewed and deployed for beta testing. Current source commit
`40d339d8` on PR #82, based on beta `v1.3.005-beta` at `89d55ca5`.
LUT enablement/files and HDR tone-map target gamma now belong to Color / Output;
general tone mapping, Target nits and Target black remain in Rendering.

Clean x64 Release rebuild and final committed Release build passed. All native
failures are resolved across the 1,130-test run and its documentation-only focused
rerun; all 14 focused Config scenarios passed. Both outstanding synthetic GPU
comparisons passed. Actual capture, continuous live LUT replacement and physical
HDMI/display response remain unverified. Deployed VP restarted and loaded the
existing configuration successfully; the configuration file was not edited.
PR: https://github.com/billslack2/videoprocessor/pull/82 (not merged).

The latest completion entry below supersedes earlier historical ownership and
LUT-input-gamma semantics. One Color selection now supplies the full calibration
contract. Prior Rendering calibration sets are preserved by the documented
migration, including manual variants for distinct sets and inactive originals.

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
| Target gamut | Rec.709 |
| Expected source SDR gamma | Gamma 2.2 |
| Display transfer | Gamma 2.2 |
| RGB output range | Full |
| Limited transport transfer | 2.2 beta, inactive while output is Full |
| HDR tone-map target gamma (with usable LUT) | Gamma 2.2 |
| Target nits (HDR tone mapping) | 100 |
| Target black (HDR tone mapping) | 0 |
| Dither target depth | 10-bit, clamped to output surface |
| Dithering | Auto (quality preset) |
| Limited + pure Gamma 2.2 flag | Derived: On only for Limited + 2.2 |

Except for documented, user-approved beta behavior changes, preserve an operator's existing explicit values during
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

### Wording and relative window-order completion

- Runtime commit c22fd327; test-only assertion correction 1d9f0859, both on PR #82.
- Direct labels: Convert SDR reference to target; Keep SDR tone values unchanged;
  Use transport transfer as SDR input. LUT input: Same as display transfer.
  BitBlt model, Saved automatic policy and When unset replace misleading labels.
  No saved configuration token or gamma behavior changed.
- Cross-process Windows fixture explicitly raises a VP-like topmost fullscreen
  surface. The strengthened test failed before the change and passed afterward.
  Config responds to VP foreground changes by checking actual relative order and
  raising without activation when covered. Popup deferral remains; no focus polling
  or native cross-process ownership. VP interaction is not blocked; clarification
  about full blocking modality remains pending.
- Clean full x64 Release rebuild succeeded. Help inventory check passed. Full UI
  run passed 65/66; remaining stale-label assertion corrected and its complete
  profile/persistence scenario passed on focused rerun. Window/popup cases passed.
- Verified 58-file Release staging. Matched host/renderer plus Config/help deployed,
  all hashes verified. Running Config process and unsaved document preserved by
  retaining loaded images in backup; tray Exit and reopen loads the updated UI.
- Backup: C:\Videoprocessor\vp\backup-before-vp0174-wording-20260908-120902.
- Active configuration edits: none. SHA256 remains
  69CDA6889972E43D80EFB2D24B01A4A9D4528041384F3E21923D4943252EE9EA.

### SDR/LUT UI refactor completed and deployed (2026-09-08)

- Commit c5aa1b74 on PR #82. Continued from verified beta tip 38e7508f4 in
  E:\codex\videoprocessor\vp-0174-sdr-lut, carrying the existing VP-0174 changes.
- Enable SDR gamma processing replaces the three-choice dropdown: checked=on,
  unchecked=passthrough. Saved off/AUTO retain semantics and are explained using
  a partially checked state until explicitly changed. Use profile default restores
  inheritance. Desired SDR gamma offers explicit values; saved AUTO remains
  readable as the BT.1886 assumption. Fresh display/desired defaults remain 2.2.
- New Rendering-owned calibration_lut_input_transfer is beside LUT enablement/files
  and applies to all three slots. An explicit value (including display) overrides
  old Color calibration_lut_input_gamma after profile selection. Omission retains
  the previous contract; no existing settings are moved or discarded on load/save.
- LUT reload identity includes input transfer; changing a display-dependent input
  also tracks calibrated display gamma. Rejected changed contracts detach rather
  than retain old LUT bytes under a new interpretation. Missing/disabled LUTs use
  physical display gamma. HDR Target nits/black and DTM remain active with LUTs.
- Preset gamuts only: Rec.709, P3-D65, BT.2020. No custom xy/HDR metadata UI added.
- Independent rendering/calibration specialist reviewed implementation and six real
  runtime logs; no blocker. Verified Rendering override, independent Color choice,
  omitted override, inherited Rendering value, explicit display, and missing-LUT
  fallback. These were separate launches, not continuous live-switch tests.
- Clean full x64 Release rebuild succeeded. Native 1,110/1,111 passed; sole docs
  inventory check corrected and passed focused rerun. Full updated UI 63/66 passed;
  three intermittent window/popup tests passed in separate focused processes.
  All changed SDR/LUT/migration cases passed. Physical HDMI response unmeasured.
- Release staging: all 58 immutable files verified. Matched host/renderer pair,
  Config and help deployed; installed hashes verified. Active configuration edits:
  none. SHA256 69CDA6889972E43D80EFB2D24B01A4A9D4528041384F3E21923D4943252EE9EA.
- Backup: C:\Videoprocessor\vp\backup-before-vp0174-sdr-lut-20260908-131507.
- Runtime evidence: C:\Users\bslac\AppData\Local\Temp\vp0174-sdr-runtime-20260908-131042.
  Existing Config session preserved; save edits, tray Exit and reopen to load update.


### LUT control cleanup deployed (2026-09-08)

- User approved removing the duplicate compatibility row instead of retaining it
  in Color / Output. The sole editable control is now **Gamma expected by the LUT**
  under Rendering > Display calibration LUT (3D LUT). Default/inheritance labels
  no longer mention the old Color storage. Older values still load internally;
  this cleanup does not change rendering math or discard saved settings.
- Commit 1256d0d6 on PR #82, built in the clean E:\codex\videoprocessor\vp-0174-lut-wording
  worktree from latest beta ee9b3f73 plus the preceding VP-0174 changes.
- Clean full x64 Release rebuild passed. All 1,115 native tests and five focused UI
  scenarios passed, including control removal/label checks, saved-value persistence,
  unified profile migration, section behavior, choice labels, and defaults.
- All 58 staging files verified. Matched host/renderer, Config/helper and help deployed;
  every deployed hash matched its staged artifact. Active configuration edits: none.
  SHA256 remains 69CDA6889972E43D80EFB2D24B01A4A9D4528041384F3E21923D4943252EE9EA.
- Backup and deployment evidence:
  C:\Videoprocessor\vp\backup-before-vp0174-lut-cleanup-20260908-141743.
  No older Config process was running at deployment; the updated Config was opened
  on Rendering afterward.


### Auto and Scaling defaults cleanup deployed (2026-09-08)

- User requested a single automatic choice and confirmed **Auto** is appropriate
  for these processing settings. Restored Auto instead of Use quality preset,
  retaining resolved-value status below the controls.
- Default Scaling profile hides and disables the redundant Use default entry for
  Upscaler, Downscaler and Anti-ringing. Other Scaling profiles explicitly offer
  Inherit from default profile, preserving the distinction between inheritance of
  an explicit scaler and selecting the quality preset's automatic scaler.
- Commit 52b1c7d3 on PR #82. Clean worktree from current beta ee9b3f73 plus prior
  VP-0174 changes. Full x64 Release rebuild passed; five focused UI tests passed:
  choice labels/effective Auto previews, profile sections, profile lifecycle,
  calibrated defaults, and every-page configuration round trips.
- Verified all 58 staging files; deployed matched host/renderer, Config/helper
  and documentation with matching hashes. No active configuration edits; preserved
  its current SHA256 04A295CFEE4213E9D68CC4376278431C1B6057E488DBCC931930B0F91BD05228.
- Backup: C:\Videoprocessor\vp\backup-before-vp0174-scaling-default-20260908-142657.
  Opened the updated Config app on Scaling after deployment.


### Requested clean rebuild and redeployment (2026-09-08)

- User explicitly requested build and deploy again. Verified GitHub beta remains
  ee9b3f73 and PR #82 head remains 52b1c7d3; the build includes both.
- Fresh full x64 Release rebuild succeeded. Choice labels and VP Renderer name
  smoke test passed, including Auto/default assertions. All 58 staging files verified.
- Deployed matched host/renderer, Config/helper and help; all installed hashes match
  this fresh build. Active configuration unchanged (SHA256
  04A295CFEE4213E9D68CC4376278431C1B6057E488DBCC931930B0F91BD05228).
- Backup: C:\Videoprocessor\vp\backup-before-vp0174-requested-rebuild-20260908-145330.
  Running Config images retained in that backup to preserve the user's open session
  and unsaved edits. Tray Exit and reopen loads the freshly rebuilt executable.


### Rebased onto merged LLDV beta and deployed for testing (2026-09-08)

- User requested waiting for their beta merge, then rebasing, rebuilding and deploying.
  PR #84 (VP-0176 Standard LLDV profiles) was merged; latest beta tip is 89d55ca5.
- Clean worktree E:\codex\videoprocessor\vp-0174-post-lldv. Rebased all nine VP-0174
  commits without conflicts. Range-diff confirms each patch is unchanged. New head
  eb7b22f3 published to PR #82 with an exact force-with-lease protecting its prior head.
- Full clean x64 Release build succeeded. All 1,115 native tests and seven focused UI
  tests passed: LLDV standard profiles, preserved LLDV metadata, unified Color/Output
  migration, SDR/LUT gamma controls, Auto labels, calibrated defaults, and every-page
  round trips. Verified all 58 staged Release files.
- Deployed matched host/renderer, Config/helper and documentation. All installed hashes
  match staged artifacts. Configuration edits: none; current user configuration SHA256
  60023E9F60E3DCD3E92D607E0D9BE880CF7DBB94CF52B9965DE8934D16538A74 preserved.
- Backup: C:\Videoprocessor\vp\backup-before-vp0174-post-lldv-20260908-152045.
  Open Config session preserved by retaining loaded images in the backup. Save edits,
  tray Exit and reopen Config before testing the newly merged LLDV/profile UI.

### REQ-004: One job per gamma setting, implemented and deployed (2026-09-08)

The user approved implementation after an independent rendering specialist review.
The current calibration contract is:

| Rendering state | Transfer behavior |
| --- | --- |
| SDR with a usable attached LUT | Keep the declared source response; encode the pre-LUT target with that same response. Physical display gamma, SDR gamma processing and HDR target gamma do not change this SDR transfer stage. |
| HDR with a usable attached LUT | Preserve the HDR source description; tone/gamut map and encode using the Rendering profile's explicit HDR tone-map target gamma, default 2.2. |
| No usable LUT | Preserve the existing physical-display gamma path and saved SDR gamma-processing behavior. HDR target gamma is inactive. |

- Rendering owns **HDR tone-map target gamma** beside Target nits and the LUT
  workflow. Color / Output uses **Display target**, **Target gamut**, and
  **Expected source SDR gamma**. The source field remains editable; inactive
  controls follow confirmed runtime attachment only when the configuration path,
  content identity and selected Color/Rendering profiles match the editor.
- `target_primaries` is canonical for SDR and HDR; the old `sdr_target_primaries`
  spelling still loads. Old Rendering `calibration_lut_input_transfer` values
  load as HDR target gamma, with `display` mapping to 2.2. Old Color
  `calibration_lut_input_gamma` loads without error but is ignored with a
  diagnostic. Canonical values win within a section; child aliases override
  inherited values. Explicit edits preserve comments while replacing old keys.
- Source and target transfers matching preserves the SDR transfer stage; gamut
  mapping, range handling, scaling and LUT correction still operate. This is not
  raw-byte passthrough. Supported LUTs must be prepared for the declared SDR
  response, selected target gamut and HDR target encoding. Existing paired SDR
  source/target luminance references and all no-LUT behavior remain unchanged.
- Independent reviewer approved beta deployment with no implementation blocker.
  A review finding about document identity forcing unnecessary live rebuilds was
  corrected before the successful final build.
- Commit `1940d790ff505d68437859198e596ec96e9d1b2b` is published to PR #82 from
  `E:\codex\videoprocessor\vp-0174-lut-workflow`, retaining the latest merged
  LLDV beta base `89d55ca5`.
- Full x64 Release build passed. All 1,123 native tests passed, including GPU
  SDR/HDR ramps, Full/Limited transport, gamut mapping, corrective LUT application,
  missing-LUT fallback, aliases and no-LUT compatibility. An additional native
  configuration-identity test passed across eight LF/CRLF, BOM and final-newline
  combinations. Nine focused Config UI scenarios passed.
- Capture smoke could not reach rendering: DeckLink input 2 reported raw=0 and
  converted=0. The test instance was closed normally. Actual capture, continuous
  live LUT replacement and physical HDMI/display response are not hardware-validated
  in this session; synthetic GPU tests do not establish optical correctness.
- Matched Release host/renderer, Config/helper and current documentation deployed.
  Every installed file hash matches its staged artifact. Backup/evidence:
  `C:\Videoprocessor\vp\backup-before-vp0174-req004-20260908-231130`.
  Active configuration edits: none; SHA256
  `1431C7A86D2362C13026372896DB83AEF015720E1F17BF1880D1669B0153FBD7`.
- The running Config session and its unsaved document were preserved by retaining
  loaded images in the backup. Save edits, use tray Exit, then reopen Config to
  load the updated application. PR #82 remains open for review.

### Complete Color / Output calibration profile deployed (2026-09-09)

- User approved moving the existing Display calibration LUT section from Rendering
  into Color / Output, with HDR tone-map target gamma inside that LUT section.
  General tone mapping, Target nits and Target black stay in Rendering.
- A selected Color profile now supplies target gamut, physical display response,
  LUT enablement, all three files, HDR gamma and output transport together.
  Rendering profile changes cannot overwrite its calibration contract. Runtime
  now applies a literal Color-root baseline before a selected child. Explicit
  `none` clears a LUT slot; omission inherits. Live inactivity follows the active
  Color profile and matching configuration path/content rather than the browsed
  Rendering profile. No production SDR/HDR pixel math changed in this move.
- Migration resolves the first effective Rendering calibration set into existing
  Color profiles, retaining explicit Color calibration choices. Other distinct or
  displaced sets become manual Color variants, copying effective Color settings.
  Existing Color names/default/rules/shortcuts remain. Extra variants have no
  invented automatic rules or shortcuts; old Rendering activation of those LUT
  sets does not automatically carry over. Complete contracts preserve explicit
  false/none/default gamma; equivalent sets are deduplicated.
- Original Rendering sections, selectors and comments survive in inactive
  `calibration_archive.*` records. Only calibration keys leave active Rendering.
  Runtime loading migrates in memory. Config saves with an exact-original backup;
  actual migration, save, reload, validation and idempotence are tested. Both
  target and older parser paths accept archived sections. No user config overwrite.
- Independent rendering specialist review approved beta deployment with no
  remaining implementation blocker. New GPU checks pass for finite-black
  BT.1886 versus 2.4 SDR and colored PQ/HLG BT.2020 mapped into P3-D65 versus
  Rec.709, including downstream LUT correction. These close the two synthetic
  validation gaps recorded in the original feedback review.
- Clean x64 Release rebuild passed. Final native run: 1,129 of 1,130 passed;
  the sole documentation-reference failure was corrected and its focused rerun
  passed. All 14 Config scenarios passed, including real save acknowledgement,
  alias migration, ownership, profile switching, None/inheritance, LUT discovery,
  section layout, every-page round trips and unchanged luminance/LLDV controls.
  Final committed Release build passed and reports `40d339d`, dirty=false.
- Source `40d339d8e5ef4a5a2ea22251966745b58db6a08a` published to PR #82 from
  `E:\codex\videoprocessor\vp-0174-calibration-profile`. Verified beta remains
  `89d55ca5`. All 58 staged Release artifacts verified before deployment.
- Deployed matched host/renderer, Config/helper and current documentation; installed
  hashes match staged files. Backup and evidence:
  `C:\Videoprocessor\vp\backup-before-vp0174-calibration-profile-20260909-083627`.
  Configuration edits: none. Preserved SHA256:
  `1431C7A86D2362C13026372896DB83AEF015720E1F17BF1880D1669B0153FBD7`.
- VP was closed normally and restarted as PID46064. Startup accepted the active
  configuration and restored its saved profile state. It remained awaiting a
  render graph, so actual capture/live LUT replacement and optical output were
  not validated. The open Config session was preserved with its loaded images in
  the backup; save edits, tray Exit and reopen Config to use the deployed UI.

### 2026-09-09: Renderer tab order

- Swapped Scaling and Color / Output at the user's request: Rendering, Color /
  Output, Scaling, Screen, Zoom, Processing. Stable page IDs and settings are unchanged.
- Source `e00c46a854af1de1632680a7fa5c7c74f2ce335b` published to PR #82.
  x64 Release solution build passed; existing rapid-tab navigation and every-page
  round-trip checks passed. All 58 staged artifacts verified.
- Deployed Release host/renderer and Config/helper; installed hashes match staging.
  Active configuration unchanged. Backup and deployment evidence:
  `C:\Videoprocessor\vp\backup-before-vp0174-tab-order-20260909-091805`.
  VP and Config were not running at deployment time.