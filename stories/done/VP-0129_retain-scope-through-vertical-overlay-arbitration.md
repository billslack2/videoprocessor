# VP-0129: Retain scope through vertical overlay arbitration

## Status

Done (2026-08-23). Accepted for completion. The user authorized implementation
after reviewing this story; GitHub was re-queried and confirmed
`v1.2.001-beta` as the default integration branch. Implementation commit
`9e19fa8bf31537aba11b020acceab99237688a3f` is pushed on
`codex/vp-0129-vertical-overlay-arbitration`, based on default-branch commit
`daed55e98566c4b109765de004b715f7e01a3690`. The clean worktree is
`C:\Users\bslac\vp\worktrees\vp-0129-vertical-overlay-arbitration`.
Pull request `#62` merged this implementation into `v1.2.001-beta` as merge
commit `e45b0aa0e5290051eb69f9413a493a49b8c40c7c` on 2026-08-15. The earlier
live-validation notes remain as follow-up evidence, not a completion gate.

Completed work is deliberately narrow:

- coarse current two-edge vertical envelopes no longer bypass dense
  arbitration while subtitle Fit is enabled;
- a Fit accepted by the existing dense two-sample policy is now routed
  explicitly into bounded outward presentation;
- provisional one- or two-edge vertical inspection and pending dense Fit retain
  the exact same-generation trusted base without renewing crop authority;
- final crop telemetry now distinguishes inspection, translation-confirmation,
  and Fit-confirmation waits; and
- immediate trusted-full-raster, invalid-input, stale-generation, horizontal,
  and explicit fail-open behavior remains ahead of retention.

Validation at exact commit `9e19fa8`:

- the three new regressions failed before implementation and pass afterward;
- all 98 focused `AlphaSourceCropPolicyTests` pass;
- the complete x64 Release solution builds successfully with clean build
  identity `v1.2.001-beta-9e19fa8` and `VERSION_DIRTY=false`;
- the complete test DLL reports 820/825, with the same five unrelated existing
  configuration/reference-fixture failures:
  `ConfigurationReferenceMatchesPublicFieldInventory`,
  `Vp0097NamedViewportsUseFileOrderAndIgnoreLabels`,
  `Vp0079OwnerVariantsResolveWithoutPersistedProfileState`,
  `ConfigEditorCoreRoundTripsEveryEditorOwnedKey`, and
  `ConfigEditorCoreValidatesEveryEditableOrderedProfileSurface`; and
- the standalone x64 Release `VideoProcessorConfigTests.exe` suite passes.

Live validation of the captured menu/subtitle sequence remains. No deployment
or configuration change was performed. Original research used deployed log
`C:\Videoprocessor\vp\logs\vp.log` from 2026-08-15 and default-branch source
commit `daed55e98566c4b109765de004b715f7e01a3690`. This remains a narrow
VP-0122 follow-up, not a crop-system redesign or global timing change.

## User story

As a scope-screen operator, I want brief menu/subtitle pixels in both encoded
bars to pass through the existing dense overlay arbitration before picture
geometry changes, so they remain visible without momentarily shrinking the
movie into black bars on all four sides.

## Problem

The latest quarter of the deployed log contains two reproducible four-sided
layout flashes after a trusted `0,360-3840,1800` crop was established:

1. At 02:10:19, sequence 1496 reported a current top-and-bottom presentation
   envelope. `Alpha source crop` immediately selected `vertical_action=fit`,
   expanded to `0,148-3840,1960`, and changed the final layout to
   `unused_axis=horizontal` (`picture=188.6,263.0-3651.4,1897.0`). On the next
   analyzed sequence, the dense scan classified both regions as overlay-like
   and logged `decision=none` with translation confirmation pending 1/2, but
   the coarse Fit continued (`0,148-3840,1966`). The next reported stable base
   is sequence 1524; backlog recovery dropped 26 intervening frames, so source
   sequence count is not a reliable on-screen duration.
2. At 02:10:20, sequence 1526 logged `Alpha vertical bar fit: candidate pending
   confirmation 1/2; retaining trusted presentation`. The immediately
   following crop decision nevertheless set `applied=0`, returned full raster
   `0,0-3840,2160`, and produced `unused_axis=horizontal`
   (`picture=467.5,263.0-3372.5,1897.0`). The trusted base returned by sequence
   1529, three source frames later.

The same sequence then accepted a bottom translation after two stable samples,
applied the configured 10-pixel buffer, and engaged the existing 500 ms drift.
That path retained scale and worked as designed. There is no log evidence that
the subtitle hold, drift durations, ambiguity hold, or target buffer caused
either flash.

An earlier full-raster interval beginning at 02:09:39 persisted through an HDMI
resynchronization and renderer restart until new trusted bars were acquired at
02:10:18. This log alone cannot distinguish a real full-raster menu from a
false observation. It is not a safe basis for delaying current trusted
full-raster authority and is outside this story.

## Direct source evidence

The two failures are separate composition gaps around rules already present in
the source:

- `AlphaSourceCropPolicy.h` defines
  `VERTICAL_FIT_CONFIRMATIONS_REQUIRED = 2` and explicitly states that one
  contradictory bar scan must not resize an established scope presentation.
  `ConfirmVerticalFit` suppresses the first dense Fit candidate as pending.
- `LibplaceboVideoRenderer.cpp` currently sets
  `genericVerticalFitConfirmed` from
  `currentDetectorTopExpansion && currentDetectorBottomExpansion`. When the
  effective classification is trusted, `ResolveVerticalBarPresentation`
  immediately converts that one coarse two-edge envelope to `FIT`. This older
  generic route therefore bypasses the later dense overlay/picture-shape
  classification and its two-sample confirmation. That is the exact ordering
  visible at sequences 1496-1497.
- The final `AlphaSourceCrop::Input` contains
  `verticalTranslationConfirmationPending`, and `Evaluate` retains the trusted
  same-generation base while that flag is set. The renderer populates it from
  translation confirmation or single-edge inspection. There is no equivalent
  final-crop input for `scopeSubtitleFitConfirmation`. As a result, the dense
  Fit helper can log that it is retaining the presentation while final crop
  evaluation sees no retention reason and fails to full raster. That is the
  exact ordering at sequence 1526.
- History corroborates the integration boundary: generic two-edge Fit came
  from commits `1c097405`/`ed351817`; VP-0122 commit `dcd2588` later added dense
  Fit confirmation. Translation-pending retention was wired by
  `9d9db5ce`/`b860fb94`, but dense-Fit-pending state was not carried into the
  final crop input.

These findings warrant a localized correction because the live decisions and
the source branches match exactly; no detector-threshold inference is needed.

## Required behavior

1. With subtitle Fit enabled and valid same-generation trusted top/bottom bar
   geometry established, treat a current coarse two-edge vertical envelope as
   evidence requiring the existing dense vertical arbitration, not as a
   confirmed Fit by itself.
2. Retain the exact trusted base rectangle during the bounded first dense
   inspection and while either dense translation or dense Fit confirmation is
   pending. Retention must not grant, renew, or replace crop authority.
3. Allow a dense overlay-like result to use the existing two-sample translation
   confirmation and same-size translation path. Allow a dense broad/deep
   picture-like result to select Fit only after the existing two consecutive
   analyzed samples.
4. Reset pending state when evidence disappears, becomes incompatible, changes
   direction/class, or belongs to a different source/raster generation.
5. Preserve immediate fail-open behavior for current trusted full-raster
   authority, invalid or unbounded evidence, incompatible geometry, horizontal
   expansion, source/raster generation changes, and explicit `FAIL_OPEN`.
6. Log coarse vertical inspection pending, dense translation pending, dense Fit
   pending, and the evidence that accepts or rejects the final action. The final
   crop reason must agree with the helper's pending/accepted state.
7. Do not change detector thresholds, global crop confirmation counts,
   subtitle hold, ambiguity hold, engage/release drift, or target buffer unless
   a new regression fixture proves a separate defect.

## Acceptance criteria

- A deterministic replay of sequences equivalent to 1496-1529 never emits an
  expanded or full-raster final source rectangle before dense arbitration has
  accepted Fit. The trusted `0,360-3840,1800` base remains applied through both
  pending states.
- A coarse two-edge envelope followed by overlay-like dense evidence does not
  change aspect ratio. After two stable translation samples, it may translate
  the same-size source rectangle through the existing drift path.
- One broad/deep dense Fit sample retains the trusted base and logs pending
  1/2. A second compatible consecutive analyzed sample accepts bounded Fit.
- Disappearing or contradictory pending evidence returns to/continues the
  trusted base without a layout cycle and without extending crop authority.
- Current trusted full-raster evidence still withdraws crop immediately; this
  path does not wait for dense Fit confirmation.
- Invalid input, stale generation, horizontal involvement, impossible
  translation, and unbounded content remain fail-open.
- Focused source-crop/vertical-presentation tests and the complete x64 Release
  test suite pass.

## Regression fixtures

Add deterministic composed-policy tests, not only helper tests, for:

- trusted scope -> coarse current two-edge envelope -> first dense overlay
  sample -> second stable overlay sample -> same-size translated crop;
- trusted scope -> first dense broad/deep two-edge Fit sample -> second
  compatible sample -> bounded outward Fit;
- pending dense Fit followed by overlay-like, absent, or contradictory evidence;
- pending translation followed by an opposing translation candidate;
- current trusted full-raster authority while either pending state exists;
- stale source generation, malformed bounds, horizontal expansion, and
  explicit fail-open while either pending state exists.

Assert final `sourceBounds`, `applyCrop`, `outwardExpanded`, and
`verticallyTranslated` results. The existing helper-only tests can pass while
renderer composition still produces the flash demonstrated by this log.

## Implementation boundary

Keep the change inside existing vertical presentation arbitration and final
source-crop input/evaluation. Reuse `EvaluateVerticalBarContent`,
`ConfirmVerticalTranslation`, `ConfirmVerticalFit`, and the same-generation
trusted-base validation. A new pending bit or a narrowly generalized
confirmation-retention field is acceptable; a new detector, timer, state
machine, or presentation mode is not.

## Non-goals

- OCR, menu/subtitle identification, or application-specific overlay rules.
- Changing active-picture pixel extraction or bar-content thresholds.
- Changing subtitle hold, ambiguity hold, drift, or buffer configuration.
- Delaying current trusted full-raster fail-open behavior based on this log.
- Addressing HDMI resynchronization, renderer restart, shader compilation, or
  backlog recovery.
- Implementing VP-0124 lookahead. VP-0124 should include this story's composed
  regression sequence as a safety baseline if both changes coexist.

## Related stories

- VP-0122: Retain scope geometry through subtitle and volume overlays.
- VP-0124: Safely accelerate outward active-picture transitions with bounded
  lookahead.
- VP-0080 and VP-0110: earlier active-picture/source-crop policy work.
