# VP-0166: madVR-style display-calibration 3D LUT after DTM

## Status

Done. Merged into `v1.3.005-beta` through
[`billslack2/videoprocessor#74`](https://github.com/billslack2/videoprocessor/pull/74)
on 2026-09-05 as merge commit `2af7f64b`. This story supersedes every earlier VP-0166 interpretation in
which a Cube replaced HDR tone mapping or produced an HDR/PQ carrier. Those
implementations and tester archives are withdrawn and are not acceptance
evidence.

Final implementation commits on the merged branch were `f2e25def`, `e32f7163`,
and `440ee833`, rebased onto `v1.3.005-beta` at `1d5c731b`. The superseded
external-HDR intermediate implementation is not part of the merged history.

## Controlling product contract

The LUT is a display-calibration stage, not an HDR tone-mapping LUT:

`HDR/PQ decode -> peak analysis -> DTM -> gamut mapping -> target gamma -> calibration Cube -> Full/Limited packing -> dither -> SDR presentation`

- libplacebo retains ownership of HDR peak analysis, dynamic tone mapping, and
  gamut mapping.
- The Cube receives normalized nonlinear RGB encoded with the configured
  display transfer, such as Gamma 2.2 or BT.1886.
- The Cube runs once as target-frame `PL_LUT_NORMALIZED`: after target transfer
  encoding and before representation/range encoding.
- Output remains tone-mapped SDR. This feature does not enable Windows HDR,
  send HDR metadata, pass HDR through, or require an HDR carrier.
- Full/Limited range and 8/10-bit transport are independent of the Cube domain.
  Legal-range packing and final dithering/error diffusion occur after the LUT.
- Loading and activation do not require EDID identity, hashes, attestation, or
  a particular wire depth.

This is the direct product requirement: calibration LUTs run after DTM and
receive display gamma.

## madVR-compatible target slots

The UI retains three optional target-gamut slots:

- BT.709 calibration LUT
- P3-D65 calibration LUT
- BT.2020 calibration LUT

The active Color Config's output/calibration target gamut selects the exact
slot. Source mastering primaries do not select it. There is no cross-gamut
fallback; a missing exact slot preserves ordinary libplacebo DTM output.

P3-D65 is a first-class target and maps to `PL_COLOR_PRIM_DISPLAY_P3`. The
pixel target is carried through VP's established SDR/P709 presentation model
with manual display-mode responsibility, just as BT.2020 pixel targeting is
kept distinct from its transport/signaling policy.

The active Color Config also owns target white, black, and display gamma.
`output_gamma: AUTO` follows the accepted presentation transfer (normally
sRGB). Enabling or disabling the Cube never selects or changes gamma; choose
Gamma 2.2, BT.1886, or another characterized display response explicitly when
the calibration was built for it. This attachment-only invariant is required
for a no-LUT versus identity-LUT comparison to be visually identical.

## Cube and failure contract

- Standard 3D `.cube` files only; dimensions 2 through 65.
- One-dimensional/shaper Cubes are rejected.
- V1 requires the default `DOMAIN_MIN 0 0 0` / `DOMAIN_MAX 1 1 1` domain.
  Non-default domains are rejected because pinned libplacebo would otherwise
  reinterpret them incorrectly.
- Paths remain constrained to the configuration directory.
- Missing, unreadable, malformed, unsafe-size, or runtime-failing Cubes detach
  safely and preserve normal tone-mapped SDR playback.
- The renderer checks selected-file size/write identity once per second so a
  same-path replacement is detected without a restart.
- Candidate parsing occurs off-side. A valid candidate swaps atomically. A
  failed replacement under the same target/path contract retains and reports
  the last-known-good Cube; a changed target/path contract cannot retain an old
  Cube.
- A runtime render failure quarantines and frees the failing Cube for that
  renderer instance, then continues with ordinary DTM.

## UI contract

- One explicit enable checkbox; default off.
- All three selectors are optional and show `None` when unset.
- Selectors and the Open LUT folder button are disabled on first open when the
  feature is off, not only after the first toggle.
- Displayed selections omit `luts/` and `.cube`; stored configuration retains
  the relative path.
- Target nits stay in Rendering. Target gamut and gamma stay in Color Config.
- Tone mapping, gamut mapping, peak detection, and dithering remain available
  while calibration is enabled.
- No external-HDR mode, pass-through mode, HDR metadata fields, legacy LUT
  inspection fields, or LUT reference overrides remain.

## Validation and acceptance

Automated acceptance must prove:

1. Target `PL_LUT_NORMALIZED` executes after Gamma 2.2/BT.1886 encoding and
   before Limited-range packing.
2. HDR/PQ BT.2020 input is dynamically tone mapped to an SDR target before an
   identity or distinctive calibration Cube is applied.
3. DTM, gamut mapping, peak detection, final dithering/error diffusion, and
   8/10-bit depth selection are unchanged by LUT activation.
4. BT.709, P3-D65, and BT.2020 target selection reaches only the exact slot.
5. Default Cube domains load and non-default domains reject clearly.
6. Same-path replacement, invalid replacement with last-known-good retention,
   missing-file recovery, runtime rejection, and teardown are safe.
7. The complete native and standalone Config suites pass from an x64 Release
   build.

Real-display acceptance should compare no-LUT, identity, and known calibration
Cubes with Windows/display HDR disengaged. Record configured target gamut,
gamma, white/black, range, bit depth, Cube checksum, active signature/status,
and measurements. A synthetic grayscale/tint Cube may prove that the stage is
active, but visual program material alone cannot validate calibration.

Automated evidence completed 2026-08-31:

- x64 Release native suite: 1,045/1,045 passed after the latest-beta rebase.
- x64 Release standalone Config/UI suite: passed in full.
- x64 Release VP Renderer, VideoProcessor GUI, and Config product builds:
  successful.
- GPU regressions verify PQ/BT.2020 peak analysis and DTM before the target
  Cube, Gamma-2.2 Cube coordinates, Limited packing after the Cube, and actual
  error-diffusion shader dispatch after calibration.
- Transactional same-path replacement, last-known-good retention/retry,
  default-domain validation, P3-D65 exact-slot selection, and LUT-toggle
  transfer invariance are covered directly.
- Final independent madVR/display-calibration and libplacebo 7.360.1 reviews
  reported no remaining actionable P1 or P2 findings.

Identity-toggle defect found and corrected 2026-08-31:

- Tester logs proved that the original Auto policy changed the target from
  sRGB with the LUT disabled to pure Gamma 2.2 with it enabled. The Cube was
  identity; the checkbox was incorrectly selecting transfer state.
- Rebased commit `455783da` removes LUT enablement from transfer resolution. The
  checkbox now only attaches/detaches the Cube.
- A `display calibration contract` log line now records enabled/attached,
  configured and resolved target transfer, carrier transfer, primaries,
  luminance, stage, and `enable_effect=attach-only` after initialization and
  every live profile change.
- The 1,045-test native suite, real GPU LUT regressions, x64 Release renderer,
  x64 Release Config build, and standalone Config/UI suite all pass after the
  correction.

Latest-beta integration refreshed 2026-09-01:

- The branch was rebased without conflicts onto current
  `origin/v1.3.004-beta` commit `e9ed97c2`.
- The rebased feature commits are `c7ff0343` and `455783da`; the remote feature
  branch was updated with force-with-lease.
- The canonical release packager verified its complete 57-file allowlisted
  layout before tester-only configuration, instructions, and Cubes were added.

Standalone local-test artifact:

- `VP-0166-455783d-rebased-3DLUT-tester.zip`
- SHA-256:
  `3AA4B645DE9CF6B09883ACFB34F3C33A81AD0FFB8C54695DD352774CF909B04A`
- Contains the x64 Release product, an isolated active test configuration,
  instructions, a synthetic identity Cube, and a grayscale activation Cube.
  It supersedes the `fd8825e`, `a317c31`, and withdrawn
  `VP-0166-21044a3-HDR-to-SDR-3DLUT-tester.zip`.

Final 005-beta integration artifact:

- `VP-0166-440ee83-005beta-3DLUT-tester.zip`
- SHA-256:
  `6758056D2EB20940600050CF83371DD7F95583583E9F88D78904801BAB117F3D`
- Produced after a successful x64 Release solution build, 1,045/1,045 native
  tests, and the complete standalone Config/UI suite. The canonical 57-file
  release layout was verified before the isolated test configuration and
  diagnostic Cubes were added.

## Follow-up boundary

The merged feature owns the correct post-DTM calibration-LUT stage and its
loading, failure, slot-selection, gamma-invariance, range, depth, and SDR
presentation contracts. Subsequent work may improve how the already-shared
calibration target gamut, gamma, white/black luminance, and LUT controls are
grouped in Config, and may add broader measured-display validation. Those are
follow-ups rather than blockers for this completed implementation.

## Non-goals

- No external HDR tone-mapping Cube and no HDR passthrough.
- No madVR proprietary binary `.3dlut` import.
- No ADL-indexed LUT banks or inter-LUT interpolation in this story.
- No EDID/hash/attestation gating and no presenter/backbuffer redesign.
- No deployment or overwrite of user configuration/LUTs as part of source
  implementation.

## Reviewed references

- libplacebo 7.360.1 target-LUT order:
  https://github.com/haasn/libplacebo/blob/v7.360.1/src/renderer.c#L2062-L2089
- libplacebo range encoding and final dither:
  https://github.com/haasn/libplacebo/blob/v7.360.1/src/renderer.c#L2580-L2600
  and https://github.com/haasn/libplacebo/blob/v7.360.1/src/renderer.c#L2709-L2722
- libplacebo Cube-domain behavior:
  https://github.com/haasn/libplacebo/blob/v7.360.1/src/shaders/lut.c#L127-L155
- DisplayCAL madVR HDR-to-SDR calibration workflow:
  https://hub.displaycal.net/forums/topic/how-to-create-a-3d-lut-file-with-icc-profile-for-madvr/
- DisplayCAL BT.2020/P3 calibration-slot guidance:
  https://hub.displaycal.net/forums/topic/3dlut-for-madvr-hdr-to-sdr2020/
- madVR Envy ColourSpace Calibration Guide:
  https://madvrenvy.com/wp-content/uploads/madVR-Envy-ColourSpace-Calibration-Guide.pdf?r=092

## Source surfaces

- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboDisplayLut.*`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboCalibrationLutPolicy.h`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.*`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboOutputPolicy.*`
- `src/VideoProcessor-Lib/RendererProfileConfig.h`
- `src/VideoProcessor-Config/ConfigEditorWindow.cpp`
- `src/VideoProcessor-Test/LibplaceboLutParserTests.cpp`
- `src/VideoProcessor-Test/LibplaceboOutputPolicyTests.cpp`
- `src/VideoProcessor-Test/LibplaceboRenderParametersTests.cpp`
- `src/VideoProcessor-ConfigTests/ConfigEditorWindowTests.cpp`
- `CONFIGURATION.html`
