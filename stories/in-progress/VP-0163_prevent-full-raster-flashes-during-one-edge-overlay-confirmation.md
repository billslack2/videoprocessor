# VP-0163: Prevent full-raster flashes during one-edge overlay confirmation

## Status

In Progress (2026-08-29). The tracker was synchronized with `origin/main`
before assignment and its 181 canonical items matched the index with no
duplicate or missing IDs. GitHub was queried directly and confirmed
`v1.3.003-beta` as the current default integration branch at
`338460cb2fe553d825a0d8fa02d5051c83bed784`. It was queried again immediately
before the NLS follow-up and had advanced to
`d0647ad5a935fd6cc054821e776a1b69b53f63ce`. The implementation branch was
cleanly rebased onto that exact current beta tip before the NLS correction.

Implementation branch `codex/vp-0163-stable-overlay-presentation` was cleanly
rebased onto that exact latest remote tip in worktree
`C:\Videoprocessor\vp\worktrees\vp0163-overlay-presentation`, then safely
force-pushed with an explicit lease. The rebased commits are `86851d3d` (crop
retention), `aa27ab23` (dark scene-cut fixture), and `d6293284` (arbitration
hardening and diagnostics).

Implementation commit `86851d3d` gives the
bounded first dense-inspection frame its own final-crop retention input, so it
uses the already-validated generation-current shared geometry instead of
depending on dense translation base state which does not exist yet. Renderer
telemetry now reports inspection, translation confirmation, and Fit
confirmation as distinct states.

Follow-up commit `aa27ab23` records the operator's dark star-field scene-cut
trigger in the focused fixture. The fixture keeps scene verification active,
marks the provisional frame pixel-unsafe, leaves dense base state absent, and
proves that vertical inspection still retains the trusted scope crop. The
focused suite remained 104/104 at that commit.

Hardening and diagnostic commit
`d6293284dff4c5dc2f40f38ab05da4bb6168886d` is pushed to the same branch. It
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

Validation at `d6293284`:

- the new first-inspection regression fixture failed before the policy change
  and passes afterward;
- all 158 focused source-crop, transition-model, and decision-timeline tests
  pass;
- the complete x64 Release solution rebuilds successfully from the beta-based
  worktree with `VERSION_DIRTY=false` and exact commit identity;
- the complete native suite reports 967/969. The only failures are
  `ConfigurationReferenceMatchesPublicFieldInventory` and
  `ConfigEditorCoreLoadsAndValidatesCurrentDeployedFixture`; both exact tests
  also fail against the untouched beta-tip Release build at `338460cb` in the
  same environment; and
- three independent final-diff reviews found no remaining concrete high-severity
  correctness, test-coverage, or logging-volume blocker.

The matched x64 Release host and VP Renderer pair was deployed to
`C:\Videoprocessor\vp` on 2026-08-29. The prior pair is recoverable at
`C:\Videoprocessor\vp\deployment-backups\vp0163-d6293284-20260829-091711`.
Post-copy SHA-256 values exactly match the build:

- `VideoProcessor.exe`:
  `A905220F1E2FA6325A32A6A5AD5F54C35506554AFE748F50B4A9DF75EBC939F7`;
- `VideoProcessorVPRenderer.dll`:
  `21EBAF71B49D6C64CBECBE4EE714F8C34EF38787A22DA99D82E9335373F66FDF`.

Active configuration, runtime state, and shader-cache hashes were unchanged by
deployment. The deployed application launched and responded with clean branch
identity `codex/vp-0163-stable-overlay-presentation`; the live log confirmed the
renderer probe and successful `API=14` plugin load from the deployed path. No
configuration edit, beta merge, or pull request was performed. Live replay of
the reported trailer/volume-overlay sequence remains the next validation step
before integration.

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

## 2026-08-29 Eternals replay follow-up

- The black scrolling-title replay reproduced repeated crop/full decisions
  while evidence was unavailable and frame-local pixel safety alternated,
  including a six-transition burst. No scene event occurred during the burst.
- Near-black unavailable evidence with a bounded vertical-only envelope may
  now enter the existing one-source inspection bridge. If dense inspection
  cannot resolve that episode, full-raster fail-open is latched until positive
  crop/full authority or new provenance; pixel-safe ambiguous frames cannot
  toggle the crop back on.
- The operator's follow-up established that NLS was not enabled during the
  black scrolling-title failure. The earlier NLS attribution is withdrawn;
  this replay is an automatic active-picture/crop-authority defect. Scene
  detection alone is not geometry authority.
- Source commit: `86244a8a`. Clean x64 Release build succeeded. Full test run:
  968 passed, with the pre-existing CONFIGURATION.html inventory test failing.

## 2026-08-29 NLS presentation-ownership follow-up

The Eternals replay also exposed a deterministic presentation shift when
standard NLS was toggled. With the viewport presenting the accepted translated
scope rectangle `0,212-3840,1812`, enabling NLS passthrough replaced it with
the raw detector rectangle `0,284-3840,1884`. The destination rectangle did
not change, so every toggle produced an exact 72-source-pixel vertical pan even
though mapping remained linear with stretch 1.0.

Commit `40d43d7e` makes an accepted viewport crop authoritative for NLS source
selection, including same-size subtitle translation, outward expansion, and
aspect-limit fill. Linear passthrough inherits that presentation. The existing
NLS-only encoded-bar removal contract remains intact when ordinary automatic
crop is off, and intentional NLS+ presentation crop remains NLS-owned. An
explicit presentation fail-open now suppresses both NLS geometry ownership and
the NLS hook for that frame, preserving the complete raster.

Focused tests cover the exact 3840x2160 translated replay, passthrough
ownership, the no-automatic-crop bar-removal exception, intentional crop, and
fail-open precedence. All four focused tests pass. The clean x64 Release build
succeeds, and the full native suite reports 971/972 with only the pre-existing
`ConfigurationReferenceMatchesPublicFieldInventory` documentation mismatch.

The matched clean Release host/plugin pair was deployed from commit
`40d43d7e` and loaded successfully at VP Renderer API 14. Its prior pair is
recoverable at
`C:\Videoprocessor\vp\deployment-backups\vp0163-40d43d7e-20260829-101348`.
The deployed SHA-256 values are:

- `VideoProcessor.exe`:
  `62BDF4FBD209B2B6B1B2313354C5EE2F807EE9880047F8A38FD3B40CF214E239`;
- `VideoProcessorVPRenderer.dll`:
  `91D6878F99E190FF83EE73D0440F237896684ED658BAF750DDF71BF2F69730B2`.

## 2026-08-29 near-black title and status follow-up

The replay beginning at approximately 10:17 confirmed that NLS was off and
behaving correctly. Scrolling program titles on a globally near-black frame
were instead misclassified as new top excluded-band content. Three stable
samples accepted an upper translation (approximately -176 source pixels),
which moved the whole presentation even though the retained movie geometry had
not changed. Later crawl geometry could also leave translation evidence ready
to apply when crop authority returned.

Commit `421543e8` constrains that path without changing normal subtitle or NLS
mapping behavior. A globally near-black frame may reaffirm retained geometry,
but different geometry is downgraded to provisional and cannot acquire or hold
bar-overlay translation authority. Telemetry now includes
`global_near_black` so the next replay can verify the gate directly. The new
`NearBlackFrameCannotReplaceRetainedPresentationGeometry` regression fixture
passes, as does the existing on-to-off NLS overlay fixture.

The same commit publishes the transient profile-change overlay for explicit
shader selections after the resolved section changes. Consequently selecting
NLS Off now reports `NLS / Off`, matching the feedback shown for the active NLS
choices; this is a UI-status correction independent of the Eternals crop fix.

Commit `00a3d9a3` restores beta-line build identity protection so descendants
of the moving default beta branch cannot inherit the historical v1.1.016 tag.
The clean build reports `v1.3.003-beta-00a3d9a` and
`VERSION_DIRTY=false`.

Validation and deployment at `00a3d9a3`:

- clean x64 Release solution build succeeded;
- 972 of 973 complete native tests passed; the sole failure is the existing
  `ConfigurationReferenceMatchesPublicFieldInventory` documentation mismatch;
- the focused NLS Off overlay and near-black geometry tests both pass;
- the matched host/plugin pair was deployed without changing configuration,
  and the live log confirms VP Renderer API 14; and
- the previous pair is recoverable from
  `C:\Videoprocessor\vp\deployment-backups\vp0163-00a3d9a3-20260829-103638`.

Deployed SHA-256 values:

- `VideoProcessor.exe`:
  `6660E7C812BC7E3A1F92A973C33805C68FE69A8418178543CB34DCE160B02F47`;
- `VideoProcessorVPRenderer.dll`:
  `8CBF3524FEB90CF94814F0B02DDB33856EE40D67176E4320E2A326E514AE7195`.

## 2026-08-29 monotonic near-black title episodes

The next Eternals replay at approximately 10:41:33–10:41:38 showed that the
first near-black geometry constraint removed subtitle translation but did not
remove crop/full-raster oscillation. Sparse scrolling text repeatedly produced
plausible full-width bar rectangles: presentation cropped at source sequences
516, 537–539, and 582–584, then failed open at 518, 540, and 585. The detector
bounds tracked the title strokes and ranged from approximately 1.57:1 to
3.96:1; they were not stable picture geometry. NLS remained off and final
mapping remained linear.

Commit `4f61b340` introduces presentation-independent global near-black
measurement and a generation-local title episode. A near-black episode without
existing crop authority latches full raster and downgrades every apparent bar
crop to provisional. An episode with a retained crop can keep it, but bounded
visible content or loss of that geometry makes one monotonic transition to
full raster; crop cannot reacquire until a real scene/source boundary. Normal
full-raster authority remains immediately safe.

Live startup validation exposed two lifecycle details which were corrected
before handoff. Commit `dc6b8089` preserves the episode across profile
publication and converts lost retained geometry to the one-way full-raster
state. Commit `292f5e4e` narrows episode reset to an actual decoded-source
generation replacement: the temporary analysis-authority reset used by profile
publication is not a source change.

The final clean x64 Release build identifies as
`v1.3.003-beta-292f5e4` with `VERSION_DIRTY=false`. Seven focused near-black,
episode, lifecycle, and retained-geometry fixtures pass. The complete suite
reports 977/979; the two failures are the existing
`ConfigurationReferenceMatchesPublicFieldInventory` documentation mismatch and
the current deployed-configuration fixture validation, neither in renderer
presentation code.

The matched host/plugin pair was deployed without configuration changes. The
previous pair is recoverable from
`C:\Videoprocessor\vp\deployment-backups\vp0163-292f5e4e-20260829-120637`.
Deployed SHA-256 values are:

- `VideoProcessor.exe`:
  `1F0FD1B9EE89452A8DB2E24BE5349632FA978D0BD622322F7CDFDD068BF25121`;
- `VideoProcessorVPRenderer.dll`:
  `6E615F64B22AE08A6520F669A4F76A0FA67A26C92E4C25FF1D16A9DC49185401`.

The deployed API-14 renderer's 12:06 live trace started the title episode at
sequence 1 with P90 4, held full raster when title pixels raised P90 to 225,
and remained `near_black_episode=full-raster` across automatic profile
boundaries at sequences 54, 135, and 160. Every recorded final layout remained
`0,0-3840,2160`, linear, centered, and untranslated. Replaying the Eternals
opening is the remaining operator validation.

## Related stories

- VP-0129: Retain scope through vertical overlay arbitration.
- VP-0122: Retain scope geometry through subtitle and volume overlays.
- VP-0124: Safe outward active-picture lookahead.
- VP-0136: Prevent transient same-axis inward aspect switches.
