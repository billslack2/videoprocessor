# VP-0163: Prevent full-raster flashes during one-edge overlay confirmation

## Status

In Progress (2026-08-29). The tracker was synchronized with `origin/main`
before assignment and its 181 canonical items matched the index with no
duplicate or missing IDs. GitHub was queried directly and confirmed
`v1.3.003-beta` as the current default integration branch at
`338460cb2fe553d825a0d8fa02d5051c83bed784`.

Implementation branch `codex/vp-0163-stable-overlay-presentation` starts at
that exact remote tip in clean worktree
`C:\Videoprocessor\vp\worktrees\vp0163-overlay-presentation`.

Implementation commit `d7f26b3d00b04d1245f00d3766b80dc7f57ffff1` is
pushed to `origin/codex/vp-0163-stable-overlay-presentation`. It gives the
bounded first dense-inspection frame its own final-crop retention input, so it
uses the already-validated generation-current shared geometry instead of
depending on dense translation base state which does not exist yet. Renderer
telemetry now reports inspection, translation confirmation, and Fit
confirmation as distinct states.

Follow-up commit `50b662c0` records the operator's dark star-field scene-cut
trigger in the focused fixture. The fixture keeps scene verification active,
marks the provisional frame pixel-unsafe, leaves dense base state absent, and
proves that vertical inspection still retains the trusted scope crop. The
focused suite remained 104/104 at that commit.

Hardening and diagnostic commit
`e6e81f12cbe7aa9aa124b60743838ac30e49e439` is pushed to the same branch. It
bounds the inspection bridge to one decoded source sequence per unresolved
authority episode; prevents sparse/noisy dropout from re-arming a spent bridge;
deduplicates active-picture, outward-picture, translation, and Fit confirmation
counts by decoded source sequence; gives horizontal refinement conflict global
fail-open precedence over every provisional crop owner; and replaces
reason-string inference with structured withdrawal causes and decision owners.

The transition-only `Alpha source crop` diagnostic now reports source sequence
and generation, structured decision owner, cut/scene state, retention outcome,
horizontal conflict, inspection episode lifecycle, current coarse evidence,
dense scan status, selected authority, and translation/Fit confirmation state.
Its suppression key excludes raw extent jitter and periodic scan cadence. On the
captured five-minute log shape, the semantic signature would reduce the 988
repetitive vertical-content lines to approximately 17 state transitions.

Validation at `e6e81f12`:

- the new first-inspection regression fixture failed before the policy change
  and passes afterward;
- all 158 focused source-crop, transition-model, and decision-timeline tests
  pass;
- the complete x64 Release solution rebuilds successfully from the beta-based
  worktree;
- the complete native suite reports 967/969. The only failures are
  `ConfigurationReferenceMatchesPublicFieldInventory` and
  `ConfigEditorCoreLoadsAndValidatesCurrentDeployedFixture`; both exact tests
  also fail against the untouched beta-tip Release build at `338460cb` in the
  same environment; and
- three independent final-diff reviews found no remaining concrete high-severity
  correctness, test-coverage, or logging-volume blocker.

No deployment, configuration edit, beta merge, or pull request was performed.
Live replay of the reported trailer/volume-overlay sequence remains the next
validation step before review or integration.

## User story

As a scope-screen operator, I want top receiver/volume overlays and bottom
subtitles to keep the established scope presentation while their one-edge
placement is being confirmed, so overlay handling cannot briefly shrink the
movie to full-raster mapping or oscillate its apparent aspect ratio.

## Confirmed live regression

The deployed v1.3.003-beta log from the Mandalorian and Grogu trailer session,
approximately 00:10:52–00:15:19 on 2026-08-29, established trusted active
picture `0,280-3840,1880` (2.4000:1) on a 2.3500:1 screen.

The operator identified the visible trigger as a scene change involving a dark
star-field/space background. This matches the recorded failing state: the new
scene produced provisional or non-contained evidence, frame-local retention
was evaluated as pixel-unsafe, and vertical inspection was pending before its
dense base/generation state existed. A scene-verification hold alone cannot
retain that combination, so the regression fixture explicitly covers a dark
scene cut with active scene hold, unsafe provisional evidence, and no dense
base.

- At 00:11:02, a bottom subtitle completed three stable confirmations and
  correctly used a same-size `+96 px` translation with smooth engage, hold,
  and release. Presentation width and aspect remained stable.
- At 00:12:34 and 00:12:47, shallow top content correctly entered the existing
  one-edge upper-placement path and used a same-size `-130 px` translation.
- Before the first top translation, presentation temporarily withdrew the
  trusted crop and exposed full raster with the reasons `latest observation
  does not reaffirm crop authority` and `bounded visible excluded-band content
  requires outward fit`.
- Later, presentation repeatedly alternated between trusted scope and full
  raster while logical trusted geometry remained unchanged, including
  full/scope or full/scope/full changes within one second at 00:11:53,
  00:14:13, 00:14:32, 00:14:34, 00:14:45, 00:15:04, and 00:15:06.

On the configured screen, scope maps across the full width while full raster
maps to a narrower horizontal picture. These presentation-only changes explain
the visible aspect/scale jumps even though the logical geometry remains 2.40.
The final full-raster transition near 00:15:15 may be genuine trailer content
and is not treated as a regression without frame evidence.

## Regression relationship

VP-0129 established that trusted scope must survive coarse vertical inspection
and dense translation/Fit confirmation. VP-0163 is a focused regression of
that completed contract: same-size translation still works, but current
one-edge/non-contained observations can reach general frame-local retention
and revoke the final presentation crop before dense vertical classification
owns the decision.

## Required behavior

1. With valid same-generation trusted crop authority, retain the exact trusted
   presentation while top-only or bottom-only excluded-band content is pending
   dense vertical classification or translation confirmation.
2. Atomically move from trusted crop to the existing same-size translated crop
   when one-edge translation is confirmed. Do not pass through bounded Fit or
   full raster.
3. Do not let a single provisional, non-contained, or non-reaffirming sample
   withdraw an unchanged trusted crop while compatible one-edge inspection is
   active.
4. Preserve dense confirmed Fit for broad/deep or compatible two-edge content.
5. Preserve immediate safety behavior for current trusted full-raster
   authority, invalid/unbounded evidence, source or raster generation changes,
   incompatible geometry, horizontal expansion, and explicit fail-open.
6. Preserve existing subtitle thresholds, buffer, hold, engage/release drift,
   active-picture thresholds, and global crop confirmation counts.
7. Make final crop telemetry identify the presentation owner and agree with
   the dense overlay pending/accepted state without logging every insignificant
   raw extent change.
8. Count confirmation evidence by decoded source sequence, not presentation
   cadence, so a repeated frame cannot satisfy a multi-frame contract.
9. Give horizontal refinement conflict fail-open precedence over every
   provisional presentation owner.

## Acceptance criteria

1. A deterministic bottom-subtitle replay remains scope -> smooth same-size
   translation -> scope and never emits a full-raster final source rectangle.
2. A deterministic top receiver/volume replay remains scope while pending,
   then uses the existing same-size upper translation and returns to scope,
   with no intervening aspect or width change.
3. One-frame bar noise and provisional non-contained evidence cannot cause a
   full/scope/full presentation cycle while trusted geometry is unchanged.
4. Repeated scene cuts cannot produce same-second presentation oscillation
   solely from unconfirmed excluded-band evidence.
5. Broad/deep or two-edge content still reaches bounded Fit after its existing
   compatible confirmations.
6. A genuine confirmed scope-to-full-raster transition still withdraws crop
   within the existing confirmation contract.
7. Stale generations, malformed bounds, horizontal involvement, impossible
   translation, and explicit fail-open remain fail-open.
8. Focused composed-policy tests and the complete x64 Release test suite pass.
9. Re-presenting one decoded frame cannot advance active-picture, outward-fit,
   translation, or Fit confirmation counts.
10. Decision telemetry identifies the owning policy state and suppresses raw
    detector jitter while still logging meaningful transitions.

## Implementation plan

1. Add composed final-crop regression fixtures for bottom translation, top
   translation, isolated non-contained evidence, dense Fit, and trusted
   full-raster authority.
2. Trace the one-edge candidate/pending state from dense vertical arbitration
   into `AlphaSourceCrop::Input`; close any route where general retention acts
   before the dense policy owns the presentation decision.
3. Retain the same-generation trusted base during compatible one-edge
   inspection without renewing crop authority, then transition atomically to
   translation or reset to the trusted base.
4. Keep confirmed Fit and true geometry-change/fail-open precedence intact.
5. Add compact state-transition diagnostics and run focused plus complete x64
   Release validation.

## Likely implementation areas

- `src/VideoProcessor-Lib/ActivePictureEvidence.cpp`
- `src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.cpp`
- `src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.h`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- focused active-picture and source-crop policy tests

## Non-goals

- OCR or application-specific recognition of subtitles, volume UIs, or menus.
- Changing translation distance, smoothing, hold durations, detector
  thresholds, or user configuration.
- Delaying current trusted full-raster authority based only on the ambiguous
  final trailer transition.
- Redesigning active-picture detection or mixed-aspect support.

## Related stories

- VP-0129: Retain scope through vertical overlay arbitration.
- VP-0122: Retain scope geometry through subtitle and volume overlays.
- VP-0124: Safe outward active-picture lookahead.
- VP-0136: Prevent transient same-axis inward aspect switches.
