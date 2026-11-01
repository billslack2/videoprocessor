# VP-0224: Auto-align the active picture to visible screen edges with signed pixel trim

## Status

Review

2026-11-01: Implementation is complete and pushed at
`a1a9aa3a5adbba00eee99fdbb2662ff1c82d5e86` on
`codex/vp-0224-auto-screen-alignment`.
PR: https://github.com/billslack2/videoprocessor/pull/244.
Independent review approved after the recorded findings were corrected.

Validation completed:

- 42/42 focused native geometry, accepted configuration/inheritance/range and
  renderer reset-policy tests passed.
- 93/93 beta-dependency geometry/GPU/temporal renderer tests passed, including
  actual pixel readback for linear, NLS and NLS+ alignment.
- 3/3 final standard-dependency Auto GPU and dependency identity tests passed.
- 3/3 existing configured-screen/anamorphic geometry regressions passed.
- Auto/Manual UI persistence, mode switching, Center, signed trim and Reset
  checks passed; Screen sections/units test passed. UI screenshot reviewed.
- Full x64 Release solution build passed. Final committed host, both renderer
  variants and Config build passed with source `a1a9aa3`, `VERSION_DIRTY=false`.
  Existing compiler/Qt deployment warnings remain; no build errors.
- Broad all-page UI test fails an unchanged beta assertion: source selects
  Expanding tab sizing while its test requires Maximum. Both lines are
  identical at the base commit; this unrelated issue was deliberately retained.
- Full native suite completed: 3,584 executed, 3,575 passed, nine failed.
  The new documentation inventory omission was fixed in `a1a9aa3a`; its
  consistency test then passed 1/1. No C++ behavior changed in that fix.
  The other eight failures reproduce 8/8, with the same test names/assertions,
  on a freshly built, clean worktree at original beta `a591ad3b`. They are
  existing detector failures, not Auto placement regressions. No detector
  code or other active aspect-ratio worktree was changed.

Remaining acceptance: live Top/Bottom/signed-trim scope-screen checks and the
merge decision. The deployed player, active configuration and other source
worktrees were left untouched to protect concurrent aspect-ratio work. This
is ready for review, not yet accepted Done or deployed.

Evidence: `../assets/VP-0224/validation.json`,
`../assets/VP-0224/auto-picture-alignment.png`; full local logs/TRX files are in
`E:\codex\videoprocessor\vp-0224-auto-screen-alignment\TestResults`.


2026-10-31: Implementation started on `codex/vp-0224-auto-screen-alignment`
in `E:\codex\videoprocessor\vp-0224-auto-screen-alignment`, from freshly
fetched `origin/v1.4.02-beta` at `a591ad3b532771911e85f8ef026958279f2065f0`.
The user requested isolated implementation through completion with tests and
permitted review agents. No deployment or detector changes are planned.

Readiness: configuration uses ResolvedViewport plus renderer parsing and live
Apply; final destination mapping is separate from detector decisions. Auto
uses centered FitScreen with no manual padding, then a shared placement helper
for active-edge mapping and bounded signed output-pixel trim. Temporal packet
selection and NLS fallback must use the same final helper. Existing subtitle
source accommodations take priority over edge pinning. Validate geometry,
configuration round trips, default compatibility, and x64 Release builds.

2026-10-31: Created from the user's Screen geometry configuration screenshot
and request. Initial story definition preceded the implementation requested later in this chat.

## Final regression comparison (2026-11-01)

The full run took about 63 minutes, including cold software-GPU shader work.
All new placement tests and existing rendering regressions completed without
failure. The single new failure concerned the public configuration inventory;
that omission is corrected and its targeted retest passes.

The following unchanged detector tests fail both on this feature build and
on the untouched original beta. The exact isolated eight-test sets match:

- `InvalidSourceOrMismatchedProposedRasterUsesNoSamples`
- `SmallBarsKeepTheExistingEighteenCodeContrastRequirement`
- `P010P210AndNativeV210AgreeOnTheExactMirroredMeasurement`
- `UntrustedAsymmetryHorizontalConflictOrInconsistentFloorUsesNoSamples`
- `TrueDimPictureBoundaryHasNativeBarProofAndAdjacentContrast`
- `ValidLayoutWithUnsupportedRgbDecoderCannotReportEvaluatedZeros`
- `QuantizedShadowedSideRejectsDeeperPictureCaptionsAndRamps`
- `QualifiedScopeReturnsWithZeroOppositeBoundaryContrast`

Baseline worktree: `E:\codex\videoprocessor\vp-0224-baseline-validation`,
detached at `a591ad3b532771911e85f8ef026958279f2065f0`, source status clean.
Its x64 Release native test build passed before the differential run.
Independent review confirmed these fixtures call unchanged CPU detector/model
functions directly and have no path through Auto alignment or renderer Apply.
These failures remain outside VP-0224; they were recorded rather than repaired
inside this placement story. The existing all-page UI tab-sizing assertion is
also unchanged from the base.

Evidence: `../assets/VP-0224/validation.json`,
`../assets/VP-0224/baseline-detector-failures.trx`, and
`../assets/VP-0224/inventory-fixed.trx`. Full original and focused TRX/logs are
retained in the feature and baseline worktrees' `TestResults` directories.

## Implementation progress (2026-10-31)

- Added per-Screen `picture_alignment_mode` (Manual default / Auto) and signed
  `picture_offset_pixels`; Manual padding is retained independently.
- Auto centers the physical screen reference and performs a destination-only
  transform after temporal source selection. Frozen packets carry their own
  trusted active bounds. Source crop, detector thresholds and authority policy
  are unchanged. Linear fallback is aligned independently, once.
- Added inverse vertical-coordinate mapping for shipped NLS/NLS+ and explicit
  unknown-mapping fallback; output bounds and subtitle accommodations limit trim.
- A separate live placement-update comparison checks all parsed configuration
  except the two new Screen keys before preserving AR/subtitle history. It does
  not broadly ignore changed configuration identity or unknown detector keys.
- Independent read-only review caught the configuration-identity and shader-name
  matching issues; both are addressed and undergoing final review.
- Added geometry, native configuration/inheritance, reset-isolation, Qt UI
  round-trip and GPU pixel-readback tests. Initial focused run passed 11/12,
  including both GPU tests; remaining fixture used a reserved profile name,
  now corrected. Test re-execution and final x64 Release build are in progress.
- First build's runtime receipt step needed a corrected process-local Windows
  PowerShell module path. A subsequent full x64 Release solution build passed.
  No deployment, active configuration modification, or other worktree edit.

## User story

As a VideoProcessor operator with a configured physical screen inside a larger
output raster, I want Auto alignment to calculate the initial vertical offset
that pins the active picture to the visible screen's top or bottom, then let
me apply a small positive or negative pixel adjustment. I also want to retain
the current manual alignment and padding behavior.

## Context

The supplied screenshot shows the Scope - Bottom Screen profile, a 2.35:1
screen, Bottom vertical picture alignment, and 280 px Screen edge padding.
Today the operator supplies that pixel value. The requested Auto mode should
derive the baseline placement from detected active-picture geometry, raster
geometry, and configured screen geometry rather than require a manually
calculated compensation for the part of the raster outside the visible screen.

Keep three rectangles distinct: the HDMI/input raster, the detected active
picture within it, and the physical screen's visible region mapped into output
coordinates. Input and output dimensions may differ. The 2.35:1 CIH setup is
display context, never a constraint on the detector's measured aspect ratio.
Reference screenshot: `../assets/VP-0224/screen-picture-alignment.png`.

## Required behavior

1. Add an explicit Manual / Auto alignment mode to Screen geometry, alongside
   the existing Top / Center / Bottom choice. Manual preserves current behavior,
   including existing raster-relative placement and pixel padding. Existing
   configurations default to Manual and render identically after upgrade.
2. Auto computes the resting placement of the detected active picture against
   the selected visible screen edge. Top pins its top edge to the visible top;
   Bottom pins its bottom edge to the visible bottom. Use measured bounds and
   existing source-to-output mapping, not only an AR label: asymmetric encoded
   bars must not be treated as centered. Derive the screen rectangle from the
   configured geometry; never assume the entire output raster is visible.
3. Apply a separately retained signed pixel trim after the Auto baseline. Use
   output-pixel units with an explicit sign convention: positive moves down,
   negative moves up, for both Top and Bottom. Zero means exact edge alignment.
   The Auto baseline must not be stored as or added a second time to the old
   manual padding. Switching modes preserves each mode's saved adjustment.
4. Center retains existing centered presentation; edge pinning and its Auto
   trim are inactive, with retained values and clear UI help. Explain the
   selected alignment reference, pixel units, and sign convention in the UI
   and configuration reference. Reset behavior must be explicit and tested.
5. Recalculate the baseline when trusted active bounds, input/output raster,
   selected Screen profile, or relevant presentation geometry changes. Use
   authoritative settled detector output and existing transition policy;
   do not add an independent detector, force a standard ratio, or feed screen
   placement/trim back into active-picture detection.
6. Treat Auto as destination placement after existing sizing/mapping choices.
   Preserve scale, source crop, selected aspect, and fitting policy. Exercise
   linear, NLS, zoom and anamorphic paths where supported. Specify deterministic
   whole-pixel rounding and avoid cumulative offsets across Apply or frames.
7. Define and validate limits before implementation: explicit trim may move
   content beyond a physical screen edge for calibration, but must not silently
   introduce source cropping, rescaling, or raster clipping. Clamp to the safe
   output range and expose effective trim and limiting reason. If picture
   height exceeds visible screen height, pin the requested edge while retaining
   existing sizing/crop policy; do not claim both edges fit.
8. Respect required subtitle accommodations through the existing presentation
   policy, and restore the Auto resting position plus trim when accommodations
   expire. Do not pin the encoded raster or subtitle envelope while reporting
   that the active-picture edge is pinned. Explain temporary displacement in
   diagnostics. For unknown/untrusted geometry use the existing safe fallback,
   report fallback explicitly, and resume Auto when trusted bounds return.
9. Persist/inherit mode and trim per Screen profile and support live Apply.
   Extend bounded layout diagnostics with mode, input/output/screen/active
   rectangles, computed baseline, requested/effective trim, final position,
   and fallback/limit/subtitle reasons.

## Acceptance criteria

- Demonstrate Top and Bottom Auto with zero trim on a configured 2.35:1 visible
  screen inside a 16:9 output raster. Assert the active-picture edge equals the
  visible-screen edge, including encoded bars and asymmetric active bounds.
  Show the resulting rectangles differ from legacy raster-edge alignment in
  a case where the visible screen and raster have different vertical bounds.
- For both edges, positive and negative trims move the picture by the stated
  output-pixel amount after Auto placement. Include safe-range limits, odd
  dimensions/rounding, no vertical slack, content taller than the screen,
  equal-aspect and pillarboxed pictures, and different input/output resolutions.
- Cover arbitrary measured aspects without standard-ratio snapping, screen and
  output changes, trusted-to-unknown-to-trusted transitions, and repeated Apply
  with no accumulated drift. Auto does not change detector bounds or authority.
- Verify subtitle entry/hold/release and supported NLS, zoom and anamorphic
  layouts. Assert unchanged source selection and picture dimensions for a
  placement-only mode/trim change; distinguish baseline from temporary offsets.
- Verify legacy configuration migration/defaults, profile inheritance,
  save/reload, mode switching with value retention, Center, Reset and live Apply.
  Existing Manual profiles, including the screenshot's 280 px value, retain
  their behavior without automatic conversion or configuration replacement.
- Relevant geometry/configuration regressions and an x64 Release build pass.
  Record visual checks on the configured scope screen for Top/Bottom, zero and
  signed trim, subtitles and aspect transitions before marking Done.

## Related work and readiness

- VP-0155: configured-screen vertical alignment, including active NLS.
- VP-0186: current Screen edge padding setting and UI.
- VP-0193: safe final-placement/padding work. Review its current implementation
  and acceptance evidence before designing the new mode; do not supersede it
  silently or assume the observed deployed behavior matches its proposed fix.
- VP-0207 / VP-0214: coordinate subtitle fit ownership where placement overlaps.

Next action: review PR #244, then perform live scope-screen acceptance and
integration when it will not disturb the concurrent AR investigation. The current feature is
isolated in its own clean E: worktree and is not deployed.

## Tracker audit

Fetched origin/main at a8c12f5d before creating the story. Found 241 canonical
records and 241 index rows, maximum root ID VP-0223, with no duplicate/missing
IDs, registry/count discrepancies or child-sequence gaps. Assigned VP-0224.
Pre-existing state discrepancies are preserved for a deliberate tracker repair:
VP-0088 and VP-0168 use `In progress` rather than exact `In Progress`; VP-0168's
index also uses that capitalization; VP-0070 lacks a `## Status` heading;
VP-0211 is in `in-progress` with `In Progress` but indexed as `Review`.
The unrelated canonical-checkout VP-0207 asset edit is preserved. This story
and registry update are isolated in a clean E: tracker worktree.
