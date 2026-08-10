# VP-0110: Smooth viewport subtitle placement in and out with millisecond timing

## Status

In Progress (2026-08-10). The developer confirmed `v1.2.001-beta` as the
implementation base. Work has started on
`codex/vp-0110-subtitle-placement` in
`C:\Users\bslac\vp\vp-0110-subtitle-placement`, based on
`origin/v1.2.001-beta` at `02a7543`. The current deployed configuration
exposes `subtitle_release_drift_seconds`; the new contract will express both
directions in milliseconds.

Implementation checkpoint (2026-08-10): the isolated source worktree now
parses and publishes `subtitle_engage_drift_ms` and
`subtitle_release_drift_ms`, generalizes the renderer's release-only drift into
one retargetable translation interpolator, and preserves the detector and hold
decision path. The Qt configuration editor exposes both millisecond controls.
Targeted x64 Release builds of VideoProcessor-Test, VP Renderer, and the Qt
configuration editor pass; unit-test execution remains in progress.

Deployment checkpoint (2026-08-10): rebased source commit `c154917` onto the
then-current `origin/v1.2.001-beta` commit `8218f1a`. Clean x64 Release builds
of the host and VP Renderer succeeded with `VERSION_DIRTY=false`. Deployed the
matching `VideoProcessor.exe` and
`vprenderer\\VideoProcessorVPRenderer.dll` pair to `C:\Videoprocessor\vp` and
verified both deployed SHA-256 hashes match their build artifacts. Backed up
the prior binary pair and active `VideoProcessor.cfg` in
`C:\Videoprocessor\vp\backup-vp0110-20260810-105221`. Preserving all existing
configuration content, the active Scope viewport now has
`subtitle_engage_drift_ms: 0` and `subtitle_release_drift_ms: 1000` in place
of `subtitle_release_drift_seconds: 1`.

Follow-up deployment correction (2026-08-10): the initially deployed host and
renderer pair was correct, but the separately packaged Qt configuration editor
was still its prior build and therefore rejected the new keys. After the user
closed that editor, replaced `VideoProcessorConfig.exe` from the same x64
Release build, verified its SHA-256 hash against the build artifact, and
reopened it for validation.

Regression correction (2026-08-10): live testing showed that a zero-duration
engage lost the remembered nonzero target, so a later subtitle-evidence gap
skipped the configured release interpolation and briefly fell back to full
raster. Restored the known-good deployment first, preserving the failed-test
configuration in `backup-vp0110-regression-20260810-143616`. To avoid mixing
unrelated uncommitted crop work in the original worktree, created the clean
`codex/vp-0110-subtitle-fix` worktree at
`C:\Users\bslac\vp\vp-0110-subtitle-fix`. Commit `2e17c32` retains a snapped
target so a zero target begins the configured release duration; its focused
test covers that sequence. x64 Release test, host, and renderer builds passed.
Deployed and hash-verified the corrected EXE/renderer pair, with the active
configuration restored to `subtitle_engage_drift_ms: 0` and
`subtitle_release_drift_ms: 1000`; the pre-fix deployment is preserved in
`C:\Videoprocessor\vp\backup-vp0110-fixed-20260810-143832`.

Second live correction (2026-08-10): the corrected release drift retained the
trusted crop, but diagnostics showed the first zero-shift frame of a timed
engage was still classified fail-open and briefly presented full raster. Commit
`f574507` marks that initial state as a valid retained trusted base; it does
not change when subtitle placement is selected. Focused policy tests and x64
Release test, host, and renderer builds passed. Deployed and hash-verified the
updated EXE/renderer pair; the immediately preceding pair is backed up in
`C:\Videoprocessor\vp\backup-vp0110-engage-base-20260810-145820`.

Live-validation corrective checkpoint (2026-08-10): with the active Scope
profile still configured for a 250 ms engage drift, the user observed the
picture make several small movements while the dense bar detector refined a
new subtitle target from approximately 192 to 210 source pixels. The developer
approved a fixed internal confirmation rule rather than another configuration
surface: a new or larger translation target must appear in two consecutive
dense analysis samples with the same direction and within two source pixels
before it reaches the existing millisecond interpolator. FIT/NONE evidence
cancels the candidate immediately. During the bounded confirmation interval,
the renderer retains the trusted crop instead of flashing full raster; the
tradeoff is that the first edge pixels may remain clipped for approximately
50-125 ms depending on cadence. This confirmation is based on analyzed
observations, not wall-clock time or raw frame count.

The corrective implementation is currently uncommitted on
`codex/vp-0110-subtitle-placement` at source HEAD `73899b0`. The focused
Alpha crop-policy suite passes 83/83. The added integration case proves that a
pending one-edge translation can cross the detector's provisional-authority
gap, receive its second dense sample, and remain on the trusted crop instead
of exposing full raster. Full native testing reports 770 passed and the same
six pre-existing configuration/reference failures reproduced on clean HEAD.
A single-process clean x64 Release solution rebuild succeeded.

Final corrective deployment checkpoint (2026-08-10): after another VP-0110
worktree briefly replaced the live binaries with its narrower snap-release
fix, redeployed the complete confirmation build and verified the deployed
artifacts against the final Release outputs. The host SHA-256 is
`73ED6C19ACD0729E1FFC45B35A37DEA17BF97081944FCF948AC8E7740A0A98DE` and the
VP Renderer SHA-256 is
`EFDA5E439583CA4562D3DBEC13ACB1B2DEF0688C336FB8DFC3652CABE1635E36`.
The immediately prior pair is backed up in
`C:\Videoprocessor\vp\backup-vp0110-complete-confirmation-20260810-144104`.
The active Scope profile preserves the user's surrounding values and now uses
`subtitle_engage_drift_ms: 0` and `subtitle_release_drift_ms: 1000`. VP is
running and responsive as `v1.1.016-beta-72-g73899b0`; startup selected
DirectShow-madVR, so user validation must select VP Renderer to exercise this
Alpha presentation policy.

Flashing-incident analysis and consolidated correction (2026-08-10): the
15:04 live trace recorded 13 engage/release pairs in roughly two seconds while
the Scope profile used `subtitle_hold_seconds: 0`. The renderer evaluated
presentation every frame but sampled dense bar content every third frame, so
the narrower deployed build expired the action on unsampled frames. It also
routed the subtitle from trusted `0,276-3840,1884` geometry to an outward
`0,276-3840,2090` Fit, changing aspect ratio. The simultaneous provisional
active-picture candidates were visible in diagnostics but were not the direct
cause of this repeatable cadence flash.

The complete correction is now committed and synchronized on
`codex/vp-0110-subtitle-placement`: `9d9db5c` preserves the existing
two-sample target confirmation and zero-hold compatibility work, and
`2a4f364` prevents coarse current-frame envelopes from retargeting a confirmed
dense motion target. One-edge subtitle motion remains a same-height source
translation and cannot become an outward Fit. New configurations enforce a
minimum `subtitle_hold_seconds` of 0.25 seconds (maximum 30; default 2), while
both transition durations retain independent `0`-means-snap behavior. The Qt
editor exposes the limit in its tooltip and uses the shared validation rule.
Native Release testing reports 772 passed and the same six pre-existing
configuration/reference fixture failures. Clean x64 Release builds of the
host, VP Renderer, and Qt configuration editor succeeded at `2a4f364` with
`VERSION_DIRTY=false`.

Aspect-stable deployment checkpoint (2026-08-10): after the configuration
editor closed, backed up the prior host, VP Renderer, Qt editor, and active
configuration in
`C:\Videoprocessor\vp\backup-vp0110-aspect-stable-20260810-155127`.
Deployed the clean `2a4f364` x64 Release artifacts and verified each deployed
SHA-256 against its build output: host
`084943972756977BBC78499B86A517EAF25BD36B93A262013919BAD9877523E0`, VP
Renderer `E598756936088AB2C491F09D78009219FC337217B612387C2EA0DE09A9D94809`,
and Qt configuration editor
`ABFE1527861BD7320D3C9DEAF33098192BDEE1538C111B4845ED7B5BDEBAB7F8`.
The backed-up/current configuration diff contains only the requested Scope
guardrails: `subtitle_hold_seconds` changed from 0 to 1 and
`subtitle_engage_drift_ms` from 1 to 250;
`subtitle_release_drift_ms` remains 1000. All VP processes remain stopped so
the user can launch the verified deployment for testing.

Authority-boundary correction and deployment checkpoint (2026-08-10): the
15:52-15:54 live trace confirmed that the translation itself was now
aspect-stable: every intermediate source rectangle retained the trusted 1608
pixel crop height and the 250 ms engage and 1000 ms release paths were
monotonic. The remaining visible defect was nine isolated full-raster frames
at two authority boundaries: the provisional frame before the first dense
confirmation sample, and the zero-displacement origin frame of a timed engage.
Commit `b860fb9` retains the same-generation trusted base for exactly those
two bounded states. It does not expand the subtitle classifier, accept
horizontal/two-edge changes, or alter when engage/release decisions occur.
Telemetry now distinguishes the inspection wait, engage base, and release
settle states. New policy tests reproduce both boundary gaps and prove that
unrelated geometry still fails open.

Full native testing at `b860fb9` reports 773 passed and the same six
pre-existing configuration/reference fixture failures. Clean x64 Release
rebuilds of the host and VP Renderer succeeded with `VERSION_DIRTY=false`.
After confirming all VP processes were stopped, backed up the previous matched
binary pair and the untouched active configuration in
`C:\Videoprocessor\vp\backup-vp0110-authority-boundaries-20260810-160929`.
Deployed and hash-verified the new pair: host
`59FB91ABF30ED7774F1BE1496B0EF124CB50E038DB152E3801D6467FD80165E0`
and VP Renderer
`ADAD9F8674BB63A7D6216B83C87F0A493EFE3101F6FCD240D2C5A776AAAD59F6`.
The deployment did not edit `VideoProcessor.cfg`; it preserves the user's
current per-profile timing values, including Scope hold 2 seconds, engage snap
(`0` ms), and 1000 ms release.

Target-buffer refinement checkpoint (2026-08-10): quick live validation across
multiple configurations showed the authority-boundary build working well and
each hold/engage/release value behaving as configured. One trace exposed a
small tail movement: a confirmed 206-pixel target completed its engage, then a
later 210-pixel extent began another tiny drift. Commit `d1ab1fe` adds
`subtitle_target_buffer_pixels`, a per-viewport nonnegative outward reserve
with a default of 10 pixels and a validated range of 0 through 50. The reserve
is applied symmetrically by direction for upper and lower subtitles, absorbs
later extent growth already covered by the accepted placement, and is clamped
at the source raster edge. A value of 0 restores exact-target behavior.
Padding remains the required visual clearance; the target buffer only
stabilizes movement and does not alter detection, confirmation, hold, or
release eligibility. The Qt editor, unified configuration, and reference
documentation expose the setting. Focused policy/configuration tests pass;
full native testing reports 776 passed and the same six pre-existing fixture
failures. Clean x64 Release rebuilds of the host, VP Renderer, and Qt editor
succeeded at `d1ab1fe` with `VERSION_DIRTY=false`.

## User story

As a VideoProcessor user watching scope content with subtitle fitting enabled,
I want the translated viewport to ease into the safe subtitle position and
ease back to its normal position, with both durations configured in
milliseconds, so the motion is deliberate and directly tunable. A duration of
`0` must snap to the target position.

## Scope

1. Replace the seconds-based release setting with
   `subtitle_release_drift_ms` and add the matching
   `subtitle_engage_drift_ms` setting for movement into a newly selected safe
   subtitle position. Both accept an integer millisecond duration, have a
   documented bounded range and default, and use `0` for an immediate snap.
2. Update the unified configuration example and `CONFIGURATION.html` so the
   active viewport variants use the millisecond names and descriptions clearly
   distinguish entering from releasing the translated placement.
3. Change only the interpolation of the already-selected subtitle shift:
   interpolate the current rendered/source-window translation toward the
   existing requested shift while active, and toward zero after the existing
   release condition occurs. A new target or direction change restarts from
   the current displayed shift without a discontinuity unless its configured
   duration is `0`.
4. Preserve subtitle classification thresholds, opposite-edge handling,
   release eligibility, and crop authority. Add one bounded internal
   stabilization rule at the detector-to-interpolator boundary: require two
   stable dense analysis samples before publishing a new or larger translation
   target, while retaining the trusted crop during that confirmation window.
   Reject newly configured subtitle holds below 0.25 seconds so a user cannot
   select a hold shorter than the scheduled analysis cadence.
5. Add `subtitle_target_buffer_pixels` as a separately configurable outward
   target reserve. Accept 0 through 50 pixels, default to 10, mirror the
   magnitude for upper/lower subtitles, and clamp it at the source raster.
   Later measured growth already covered by the buffered target must not start
   another movement; `0` must retain exact-target behavior.
6. Add focused tests for zero-duration snapping, nonzero engage and release
   interpolation, retargeting during motion, and unchanged detection/hold
   decisions. Complete a clean x64 Release build and the relevant native test
   suite.

## Acceptance criteria

- Subtitle fitting can visibly and smoothly enter as well as release a selected
  translation.
- `subtitle_engage_drift_ms=0` and `subtitle_release_drift_ms=0` snap to the
  corresponding target position.
- A growing subtitle extent does not expose a sequence of intermediate motion
  targets: two stable analyzed observations select one target, followed by one
  configured snap or drift.
- Confirmation never converts one-edge overlay evidence into a full-raster or
  outward-Fit flash; genuine FIT/NONE evidence cancels pending confirmation.
- A confirmed one-edge target is not retargeted by coarse frame-local envelope
  evidence; every intermediate source rectangle keeps the trusted crop height
  and aspect ratio.
- `subtitle_hold_seconds` accepts 0.25 through 30 seconds; the editor and
  runtime share this validation, while zero remains valid for both drift
  durations.
- `subtitle_target_buffer_pixels` accepts 0 through 50, defaults to 10, works
  for both translation directions, and cannot move the source rectangle beyond
  the raster edge.
- The configuration and documentation contain no active seconds-based setting
  for these two movement durations.
- Existing rules for detecting, holding, changing direction, and releasing
  subtitle placement are behaviorally unchanged.

## Non-goals

- Altering subtitle classification thresholds, OCR, black-bar sampling, or
  subtitle timing beyond the bounded two-sample target confirmation and the
  explicit 0.25-second configuration floor added above. The target buffer may
  stabilize a confirmed target but does not broaden subtitle classification.

## Implementation gate

`billslack2/videoprocessor` reported `v1.2.001-beta` as its GitHub default
branch on 2026-08-10, and the developer confirmed it as the implementation
base.

## Related work

- VP-0087: VP-managed subtitle fit with madVR presentation.
- The existing viewport subtitle-fit and `subtitle_release_drift_seconds`
  deployment work.
