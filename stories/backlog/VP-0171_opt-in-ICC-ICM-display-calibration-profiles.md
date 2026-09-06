# VP-0171: Opt-in ICC/ICM display-calibration profiles for VP Renderer

## Status

Backlog (2026-09-06). Proposed after VP-0166 established the safe, post-DTM
display-calibration Cube contract.

## User story

As a VideoProcessor operator with a measured display/projector ICC or ICM
profile, I want to select that profile explicitly for my VP Renderer display
target, so VP can apply its calibrated color transform without requiring me to
first export a separate `.cube` file.

## Controlling product contract

ICC/ICM support is an optional **display-calibration** feature. It must not
turn VP Renderer into a general desktop color-management system or change the
meaning of the established HDR-to-SDR pipeline.

Before implementation, the exact libplacebo ICC projection and its color
coordinates must be proven and recorded. The resulting transform must execute
once in the output-calibration portion of the pipeline, after VP/libplacebo
have decoded the source and performed peak analysis, tone mapping, gamut
mapping, and the configured target conversion, and before final carrier-range
packing and dithering. It must never cause a second tone map, a second gamut
map, a double transfer conversion, or a change to output range/depth.

ICC/ICM selection is explicit and attach-only, just like VP-0166's Cube
selection. Enabling, disabling, loading, or rejecting a profile must not by
itself alter target gamut, target white/black, output gamma, presentation
contract, Windows HDR state, or display-mode signalling. An identity-equivalent
profile must therefore be a no-op relative to the no-profile path.

## Required behavior

1. VP Renderer offers optional, explicit ICC/ICM display-profile selections
   for the same exact calibration targets as the Cube contract: BT.709,
   P3-D65, and BT.2020. The configured VP display target—not source mastering
   primaries—selects the applicable profile. There is no cross-target fallback.
2. A target can have one active calibration source: either its existing
   `.cube` or its ICC/ICM profile. The UI and configuration make this choice
   unambiguous; VP must never silently stack a Cube and ICC transform.
3. The implementation uses the bundled libplacebo/LittleCMS ICC facility,
   caches the profile-derived GPU transform, and does not perform profile
   parsing or transform generation on the presentation-critical path.
4. Accept only RGB display profiles that the pinned libplacebo/LittleCMS
   combination can parse and project safely. Reject malformed, unsupported,
   non-RGB, excessive-size, or otherwise unsafe profiles with a concise
   actionable status and continue with ordinary uncalibrated VP output.
5. Profile paths remain constrained to VP's calibration/profile directory.
   Do not auto-select the active Windows display profile, enumerate arbitrary
   system profiles, accept a profile supplied by the input stream, or fetch
   profiles from a network location.
6. Profile replacement follows the existing transactional calibration-file
   policy: validate a candidate off the rendering path; atomically replace a
   valid active transform; retain the last-known-good transform only for a
   failed same-path, same-target replacement; and retry after a later file
   change. A changed path or target detaches the old transform first.
7. The Config UI and OSD/status diagnostics identify calibration state as
   disabled, active Cube, active ICC/ICM, or rejected, and record the selected
   target, resolved transform stage, profile checksum/version, and rejection
   reason without exposing unrelated Windows profile state.

## Scope

- Add explicit configuration fields, profile inheritance semantics, validation,
  and native Config UI controls for per-target ICC/ICM selection.
- Add a bounded, constrained ICC/ICM loader and lifecycle owner around the
  existing libplacebo ICC APIs and shipped LittleCMS runtime.
- Establish and test the exact ICC transform coordinates/order relative to
  libplacebo target conversion, VP-0166's Cube stage, range packing, and final
  dithering.
- Add deterministic test profiles/fixtures and automated coverage for valid
  RGB profiles, invalid/non-RGB profiles, each target slot, profile/Cube
  exclusivity, reload/last-known-good behavior, and no-calibration parity.
- Update the public configuration reference with the profile contract,
  supported profile class, selection rules, and clear operator guidance on
  matching the profile to VP's configured target gamut and transfer.

## Non-goals

- madVR proprietary `.3dlut` import, conversion, or emulation.
- Auto-use of Windows Color Management profiles, GPU gamma ramps, EDID-based
  profile choice, automatic monitor matching, or desktop-wide color
  management.
- ICC profiles embedded in video/image content, source-profile color
  management, printer/CMYK/Lab/device-link workflows, or arbitrary creative
  looks.
- 1D-shaper plus 3D-LUT chains, arbitrary calibration-stack ordering, or
  simultaneous ICC and Cube application.
- Changing VP-0166's HDR-to-SDR, output range, dithering, bit-depth, DXGI,
  Windows HDR, or presentation contracts.

## Acceptance criteria

- An operator can explicitly configure a valid RGB `.icc` or `.icm` profile
  for one exact BT.709, P3-D65, or BT.2020 VP calibration target; changing the
  target selects only its matching profile.
- Before coding the production path, an automated color-coordinate test proves
  the actual libplacebo ICC API placement and verifies exactly one target
  transfer conversion, before range packing and final dithering.
- HDR/PQ and SDR regression cases prove that ICC activation preserves VP's
  existing peak detection, tone mapping, gamut mapping, output gamma policy,
  range, output bit depth, and final dither/error-diffusion behavior.
- A valid profile activates without presentation stalls after initial
  preparation. Invalid, non-RGB, malformed, oversized, missing, or changed-
  target profiles are rejected safely and leave ordinary VP output active.
- Same-path profile replacement is atomic; a failed replacement retains the
  prior validated transform only under the same target/path contract and
  recovers when a valid replacement arrives.
- Profile/Cube selection is mutually exclusive and observable in Config, OSD,
  and logs. A no-profile path and an identity-equivalent profile demonstrate
  no unintended transfer, gamut, or range change.
- Focused automated coverage passes, followed by a successful x64 Release
  build and the relevant native, Config, and GPU regression suites. Tests do
  not modify the operator's active Windows color settings or deployed
  calibration files.

## Dependencies and readiness

- VP-0166 owns the current target-frame Cube lifecycle, exact target slots,
  post-DTM calibration boundary, and transactional reload behavior; preserve
  those invariants rather than duplicating them.
- The currently bundled libplacebo build already includes the ICC API and its
  LittleCMS runtime. Before implementation, verify the exact pinned libplacebo
  version, compile-time ICC capability, LittleCMS runtime version, and the
  profile classes supported by that build.
- Complete a focused security/readiness review for untrusted binary ICC input:
  size/file-location restrictions, parser failure containment, dependency
  versioning, update policy, reload thread ownership, and test fixtures must
  be agreed before exposing the fields in a release build.
