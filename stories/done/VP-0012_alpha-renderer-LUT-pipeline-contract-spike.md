# VP-0012: Alpha renderer LUT pipeline contract spike

## Status

Done 2026-07-27. The spike selected target-frame calibration with
`target.lut` / `PL_LUT_NATIVE`; image and render-parameter LUTs remain unused.
VP-0011 implemented that decision and merged with this story through
[billslack2/videoprocessor#6](https://github.com/billslack2/videoprocessor/pull/6)
into `v1.1.014-beta` at `fefb9f19b685d589aee8a92d84988d4cac37a7ad`.

The accepted design requires target primaries, transfer, range, reference nits,
and the verified DXGI/swapchain contract to match before activation. It rejects
P3-D65 until a verified Windows P3 path exists, rejects 1D/malformed/unsafe
files, and continues playback without a LUT on rejection or a render error.
The D3D11 compatibility result is a safe single-pass fallback: disable only
error-diffusion dithering for a valid target LUT. On 2026-08-08 VP-0100 became
the separate prerequisite for proving renderer-owned output code values, and
VP-0101 became the remaining production LUT/output story. VP-0029 was closed
as superseded and its final-dither question was absorbed into VP-0101.

Final merge validation: `Release|x64` built with zero warnings/errors and
`VideoProcessor-Test.dll` passed 79/79 tests, including all supplied 65^3
cubes plus WARP identity, non-identity, and high-quality compatible target-LUT
readback coverage. Real tester validation accepted the feature for merge.

<!-- Historical review record retained below. -->

Review — implementation and automated validation completed 2026-07-25 on
`VP0011+0012`, based on `origin/v1.1.014-beta`. Draft PR
[billslack2/videoprocessor#6](https://github.com/billslack2/videoprocessor/pull/6)
contains the VP-0011 baseline (`bbe75df`), completed target/output contract
(`7b4d00c`), the independent-review hardening follow-up (`38f2ee7`), and the
handle-bound relative-path containment follow-up (`f725d13`).
The D3D11 error-diffusion compatibility/fallback follow-up is `ccc3c06`.

The selected design attaches calibration only to the final target frame as
`target.lut` / `PL_LUT_NATIVE`; render-parameter and image LUTs remain null.
Activation requires the target primaries, transfer, range, and nits to match
the declared profile and the accepted DXGI signal. Rec.709 and BT.2020
contracts are supported only when their exact DXGI color space is accepted and
the returned libplacebo swapchain-frame metadata agrees. BT.2020 is flip-model
only; a BitBlt path blocks rendering rather than claiming an active calibrated
target. A failed negotiation resets the swapchain hint to the accepted Rec.709
fallback before a LUT can activate.
P3-D65 profiles are rejected until a verified Windows P3 signal path exists.
1D, malformed, unreadable, oversized, unsafe-dimension, and path-traversal
inputs are rejected with no-LUT playback and concise log/OSD diagnostics.

Validation evidence:

- `Release|x64` solution build: zero warnings and zero errors.
- `VideoProcessor-Test.dll`: 72/72 tests passed.
- All three supplied 65³ cubes loaded through the production bounded parser.
- WARP readback proved no-LUT and identity output match, while an extreme
  target LUT transforms the expected red sample to green.
- A high-quality WARP target-LUT readback passes with only error diffusion
  removed, proving the compatible fallback preserves the LUT result.
- Independent final review confirmed target-stage placement, complete
  signal-contract checks, rejection fallback, and truthful Ctrl-I states.
- Follow-up DXGI review confirmed the prior BT.2020/limited P1 blockers are
  resolved. The parser rejects mixed 1D/3D declarations. Relative LUT paths
  are lexically constrained first, then their final junction/symlink target is
  checked on the exact handle that is read; a deterministic regression test
  accepts an in-root file and rejects an out-of-root file as `bad path`.

Remaining review is real projector/display validation of Rec.709 and BT.2020
profiles, fullscreen/window transitions, and rule/source switching. Do not
move this story to Done until that evidence and the merge/release decision are
recorded.

### Phase 1 decision record — target-frame calibration LUT

**Evidence reviewed (2026-07-25):** VP bundles libplacebo **7.360.1**
(`3rdparty\\libplacebo\\README.txt`, API 360). Its exact tagged renderer source
uses an image-frame `PL_LUT_NATIVE` before image-to-target colour conversion,
but applies a target-frame `PL_LUT_NATIVE` only after
`pl_shader_encode_color`. `PL_LUT_NORMALIZED` is a distinct linear/normalized
path and `PL_LUT_CONVERSION` replaces ordinary conversion, tone mapping, and
related colour-management behavior; neither is the initial calibration path.

mpv's `vo_gpu_next` independently follows this separation: its normal/image
LUTs are attached to the image or render parameters, while its display
`target-lut` is attached to the target frame. mpv documents that target LUTs
receive normalized RGB after encoding to the selected target colourspace
(including the target transfer). The bundled MPC Video Renderer compatibility
library has no `.cube`/3D-LUT implementation to adopt.

**Provisional implementation decision:** VP's display/projector calibration
LUT will be parsed and cached once, then attached to the per-frame
`baseTarget`/target copy as `target.lut` with `target.lut_type =
PL_LUT_NATIVE`. It must *not* be assigned to `pl_render_params.lut` or an
image frame. This places the LUT after libplacebo has converted, tone mapped,
gamut mapped, and encoded the source to the explicitly declared LUT reference
target. The target's declared primaries, transfer, range, and reference nits
therefore become mandatory configuration and diagnostic data, rather than
metadata inferred from a `.cube` file.

This is a source/API decision only. It still requires the rendering read-back,
identity/non-identity, HDR/P3 gamut-stress, DXGI/DWM, and real-display
validation listed below before it can be treated as production acceptance.

**Baseline:** On source commit `fc3cd35`, full `Release|x64` solution build
passed with zero warnings/errors and `VideoProcessor-Test.dll` passed 49/49
tests before VP012 source changes.

## User story

As the maintainer of the experimental alpha renderer, I need a tested decision
on the exact 3D-LUT pipeline position and color contract before exposing any
calibration-LUT configuration, so the renderer cannot silently apply a LUT in
the wrong transfer/gamut space or apply a second conversion afterward.

## Why this is required

VP-0011 is for display/projector calibration LUTs, not creative source LUTs.
The bundled libplacebo API has several LUT attachment mechanisms whose meanings
are materially different:

- an image-frame LUT is applied in normalized RGB and is not automatically a
  post-tone-map output-calibration stage;
- a target-frame LUT has different conversion semantics;
- `PL_LUT_NATIVE`, `PL_LUT_NORMALIZED`, and `PL_LUT_CONVERSION` are not
  interchangeable, and conversion LUTs bypass ordinary color mapping.

Current alpha rendering creates a Rec.709 SDR swapchain target and can request
an SDR transfer/range through DXGI. It does not yet expose a render reference
primaries target or a post-calibration pass. DWM-composed presentation can
negotiate a different active signal than a requested setting, so a requested
gamma is not by itself proof of the signal reaching a projector.

## Scope

This is an engineering investigation. It may add test-only code, temporary
instrumentation, or an isolated renderer harness, but must not expose a user
LUT setting, change the released configuration schema, or claim production LUT
support.

Work from a clean worktree based on the current alpha renderer. Preserve the
existing no-LUT renderer output.

## Required investigations

1. **LUT placement and type**
   - With the bundled libplacebo version, test identity and visibly non-identity
     `.cube` LUTs attached to the image frame and target frame.
   - Determine the actual ordering relative to source decoding, HDR tone
     mapping, gamut mapping, output transfer/range encoding, and presentation.
   - Test only explicit `PL_LUT_NATIVE` and `PL_LUT_NORMALIZED` meanings that
     match the proposed contract. Do not use `PL_LUT_CONVERSION` merely to make
     a result appear on screen, because it replaces normal conversion/tone-map
     behavior.
2. **Reference-target and P3 path**
   - Prove a PQ/BT.2020 source can be tone/gamut mapped to a declared P3-D65 /
     gamma 2.20 reference before a calibration LUT, without reinterpreting P3
     mastering-display metadata as source primaries.
   - Exercise a gamut-stress sample outside P3 but inside BT.2020 and compare a
     roll-off mode (`perceptual` or `softclip`) with a clipping mode
     (`relative`).
   - Establish whether the high-level target-frame route can express this
     safely. If not, prototype the minimum supported offscreen/intermediate
     target plus final pass necessary to do so.
3. **Range, transfer, and presentation boundary**
   - Trace and log source frame metadata, selected reference target, LUT stage,
     target frame metadata, requested DXGI color space, and the active/
     accepted DXGI color space where available.
   - Validate that an identity LUT does not change known output samples and
     that a non-identity LUT changes only the expected samples.
   - Establish whether full and limited range can each be proven on this
     presentation path. If limited cannot, document full-only as the initial
     production boundary and reference VP-0004 as the follow-up.
   - Specifically test that no post-LUT gamut, transfer, or range conversion is
     introduced by the proposed path.

## Deliverables

1. A concise decision record added to this story containing:
   - selected LUT attachment/pass design;
   - explicit libplacebo LUT type and why it is correct;
   - input/reference/output contracts and their ordering;
   - supported initial primaries, transfers, ranges, and presentation path;
   - observed DXGI/DWM limitations and fallback behavior.
2. Reproducible test evidence: command/test fixture, input colors, expected
   results, and actual captured/read-back results. An identity LUT and a
   diagnostic non-identity LUT are mandatory.
3. A proposed minimal configuration mapping for VP-0011 using the existing
   `[display]` and `[display_rules.name]` inheritance model. Do not use a new
   `[libplacebo]` section unless the renderer configuration loader is
   intentionally redesigned and that redesign is separately justified.
4. A list of any VP-0011 wording/configuration changes required by the result.

## Acceptance criteria

- A maintainable, post-tone-map/post-gamut-map calibration-LUT path is proven,
  or the story records a concrete technical blocker and recommended alternative.
- The path states exactly where gamma/range/primaries conversion occurs and
  proves no unrequested conversion follows the LUT.
- The P3 calibration scenario retains BT.2020 as the HDR source representation
  and uses configurable gamut mapping to reach the P3 reference target.
- The initial range/presentation support boundary is explicit and tested.
- VP-0011 has been updated with the decision and can truthfully move forward
  from `Backlog`, or remains in `Backlog` with the next required action.

## Suggested validation status record

When work starts, replace this section with branch, commit, toolchain, GPU,
driver, Windows presentation mode, test fixtures, results, and remaining
real-projector validation. Do not move this story to Done from source review
alone.
