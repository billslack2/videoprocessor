# VP-0011: Alpha renderer color-managed 3D LUT support

## Status

Review — VP-0011 and VP-0012 are combined on `VP0011+0012` in draft PR
[billslack2/videoprocessor#6](https://github.com/billslack2/videoprocessor/pull/6),
targeting the repository default `v1.1.014-beta`. The former VP011-only PR #5
is closed as superseded.

The implementation now uses the VP-0012 decision: an optional 3D `.cube` is
parsed once, contract-validated, and attached only to the final target frame
with `PL_LUT_NATIVE`. Invalid files, 1D cubes, unsafe paths/dimensions, profile
mismatches, and unverified output signaling are ignored safely while playback
continues without a LUT. Ctrl-I reports Disabled, Loaded/validating, Active, or
a concise Rejected reason.

`Release|x64` builds with zero warnings/errors and all 71 tests pass, including
the three supplied 65³ cubes and deterministic identity/extreme WARP GPU
readback. Follow-up commit `38f2ee7` made BT.2020 flip-only, resets failed
negotiation hints to Rec.709, and requires the returned swapchain frame to
match the accepted output contract before a LUT can activate. Follow-up commit
`f725d13` closes the relative-path reparse-point race: it resolves containment
and reads the same opened LUT file handle, rejecting a file outside the config
directory as `Rejected: bad path`. Automated/source review is complete.
Real-projector Rec.709/BT.2020 validation,
display-rule/source switching, and fullscreen/window transitions remain before
Done. P3-D65 reference profiles remain intentionally rejected until VP has a
verified P3 Windows presentation contract.

## Prerequisites

1. Review [VP-0012](VP-0012_alpha-renderer-LUT-pipeline-contract-spike.md)
   alongside this story. Its source/API and automated GPU evidence establishes
   the target-frame stage; real-display evidence remains before acceptance.
2. Record the selected output architecture in this story before changing
   production configuration or renderer code. It must state whether a target
   frame LUT is demonstrably post-tone-map/post-gamut-map, or whether VP needs
   an intermediate render target and explicit final pass.
3. Record the complete color contract for at least the initial supported paths:
   source representation, LUT reference primaries/transfer/range/nits, LUT
   stage, swapchain/DXGI signal, and the absence of a post-LUT conversion.
4. Resolve the limited-range boundary: either VP-0004 is Done for the
   selected presentation path, or initial LUT support explicitly supports and
   tests full-range only. Do not imply calibrated limited-range output works
   without evidence.
5. Start implementation from a clean worktree/branch based on the current
   alpha-renderer code. Do not reuse a checkout with unrelated staged,
   unstaged, or generated work.

The first implementation task is deliberately a technical spike, not user
configuration or release code. Its result may narrow the first supported LUT
profiles or require a small final-pass architecture.

## User story

As a user of the experimental alpha renderer, I want to apply a calibrated 3D
LUT to the renderer's intended SDR output, optionally selected by existing
display rules, so projector/display calibration can be applied without an
external renderer or a custom shader chain.

## Context

The alpha renderer is implemented in:

`src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`

It uses the high-level libplacebo renderer API:

`pl_render_image(renderer, &renderImage, &target, &renderParams)`

The bundled libplacebo headers already provide the required native 3D-LUT API
in `3rdparty\libplacebo\include\libplacebo\shaders\lut.h`:

- `pl_lut_parse_cube` parses `.cube` LUT text into `pl_custom_lut` data;
- `pl_lut_free` releases parser-owned LUT data;
- `pl_frame` has `lut` and `lut_type` fields used by `pl_render_image`;
- `PL_LUT_NORMALIZED` and `PL_LUT_CONVERSION` describe distinct color-pipeline
  positions and must not be confused.

The current alpha renderer has no user LUT configuration, file loading, LUT
cache, or attachment to `renderImage`/`target`.

## Critical color-management requirement

The initial feature is for a display/projector calibration LUT, not a source
creative look LUT.

For HDR input, VP first decodes the captured input and libplacebo tone maps and
gamut maps it into an explicitly configured **LUT reference target**. The
calibration LUT must be applied at the **output-calibration stage**, after
those operations. Applying a projector SDR LUT directly to
PQ/HLG/BT.2020 source values before tone mapping is incorrect.

The LUT reference target is a contract, not merely a display preference. For
example:

- a Rec.709 / BT.1886 calibration LUT expects Rec.709 values encoded with
  BT.1886 before it maps them to the projector's calibrated/native drive;
- a P3-D65 / gamma 2.20 HDR-to-SDR calibration LUT expects P3-D65, gamma 2.20
  reference values before it maps them to the projector's calibrated/native
  drive.

VP must render to that declared reference primaries/transfer/range/nits target
before applying the LUT. It must not apply a second transfer conversion after a
calibration LUT simply because the projector's physical/native response differs
from the LUT reference target.

HDR video is normally signalled as BT.2020/PQ (or HLG), even when its mastering
display metadata describes a P3-D65 display and most image colors fall inside
P3. Treat the BT.2020 container/primaries as the source. Do **not** default to
an early BT.2020-to-P3 matrix conversion: that loses the opportunity for
tone/gamut mapping and can hard-clip legitimate BT.2020 colors. Instead, use
P3-D65 as the configured gamut-mapping target immediately before the LUT when
the calibration LUT expects P3-D65.

Do not rely only on `.cube` metadata: libplacebo treats LUT input/output
metadata as informative and does not automatically guarantee the desired
conversion order. VP must explicitly select and document the LUT stage.

## Scope

Support static `.cube` 3D LUT files for the alpha renderer only. The initial
feature applies one selected output-calibration LUT to an explicitly declared
reference render target. The common initial targets are SDR Rec.709 / BT.1886
and SDR P3-D65 / gamma 2.20, with an explicitly declared range and reference
nits.

Use the existing `VideoProcessorRenderer.cfg` and display-rule machinery to
select a LUT at renderer initialization and source-state/rule changes.

## Non-goals

- Do not add LUT support to DirectShow/external renderers.
- Do not implement arbitrary user shader/LUT ordering or LUT stacks.
- Do not treat a calibration LUT as a substitute for correct input metadata,
  SDR target nits, gamut mapping, display range, or output transfer settings.
- Do not automatically apply arbitrary LUTs based solely on embedded `.cube`
  metadata.
- Do not assume that static HDR mastering-display metadata makes the encoded
  source P3. It remains source metadata used to inform rendering decisions.
- Do not reread, parse, allocate, or upload a LUT for every video frame.
- Do not add ICC-profile support in this story; libplacebo has separate ICC APIs
  and that deserves its own design if wanted later.
- Do not implement arbitrary chains of 1D and 3D LUTs in this story. Reserve a
  well-defined pre-LUT shaper / 3D LUT / post-LUT shaper extension point for a
  later story.

## Configuration design

1. Add a disabled-by-default base setting, for example:

   ```ini
   [display]
   output_lut_file=
   lut_reference_primaries=REC_709
   lut_reference_transfer=BT1886
   lut_reference_range=LIMITED
   lut_reference_nits=100
   gamut_mapping=perceptual
   ```

   An empty file value means no LUT. `lut_reference_*` specifies the exact
   signal the LUT expects as its input. It is separate from any descriptive
   information about the calibrated projector/native output.
2. Add an optional display-rule override using the same rule-selection lifecycle
   already used for alpha display settings. A rule-specific value replaces the
   base LUT only when that rule wins. Example intent:

   ```ini
   [display.sdr_projector]
   rule=$eotf==SDR
   priority=100
   output_lut_file=LUTs\Projector-SDR-Rec709.cube
   lut_reference_primaries=REC_709
   lut_reference_transfer=BT1886
   lut_reference_range=LIMITED
   lut_reference_nits=100

   [display.hdr_to_sdr_projector]
   rule=$eotf==PQ|HLG|HDR
   priority=100
   output_lut_file=LUTs\Projector-HDR-P3-Gamma22.cube
   lut_reference_primaries=P3_D65
   lut_reference_transfer=2.2
   lut_reference_range=LIMITED
   lut_reference_nits=100
   gamut_mapping=softclip
   ```

3. Resolve relative paths against the executable/configuration directory, reject
   directory traversal outside that root unless an absolute path was explicitly
   configured, and log the resolved path without logging file contents.
4. Support only documented primaries (`REC_709`, `P3_D65`, `BT_2020`), transfer
   (`BT1886`, `sRGB`, and supported numeric power values), range, and nits
   values. Reject unknown/contradictory declarations and leave playback in
   no-LUT mode rather than guessing.
5. The existing `gamut_mapping` setting controls the
   BT.2020/PQ-or-HLG-source-to-LUT-reference gamut reduction when a LUT is
   active. Expose supported libplacebo modes with user-facing names; at minimum
   document perceptual/soft roll-off and relative/colorimetric clipping
   behavior. The default must preserve the existing no-LUT behavior; do not
   silently change gamut mapping when no LUT is enabled.
6. Optionally record `lut_output_profile` (for example, `Epson native`) and
   `lut_output_transfer` as descriptive calibration/reporting fields only.
   They must not cause a post-LUT color transform. Document that `.cube` files
   do not provide reliable, portable gamma/primaries metadata, so these values
   are user-declared rather than inferred.

## Implementation plan

1. Include the libplacebo LUT header in the alpha renderer and add RAII-owned
   state for:
   - resolved file path;
   - file-content signature/hash and modification identity;
   - parsed `pl_custom_lut` pointer;
   - GPU/LUT shader state needed by libplacebo;
   - selected rule and declared LUT stage.
2. At alpha renderer initialization and display-rule/source-state change:
   - resolve the selected `output_lut_file`;
   - resolve and validate the complete LUT reference-target contract;
   - load the file once with a bounded file-size limit;
   - parse it with `pl_lut_parse_cube`;
   - validate that it is a three-dimensional LUT and its dimensions are within
     supported/resource-safe limits;
   - retain it until replacement, renderer reset, or destruction.
3. Configure libplacebo's render target to the LUT reference primaries,
   transfer, range, and nits. For HDR sources, decode from the encoded source
   space, tone map, and gamut map into that reference target before the LUT.
   Preserve the source BT.2020 container and static HDR metadata through this
   process; P3 mastering metadata must not be treated as a replacement source
   colorspace.
4. Attach the parsed LUT to the correct libplacebo render stage so it operates
   on the final reference output, after input decoding, HDR-to-SDR tone mapping,
   and gamut mapping. Use an explicit `pl_lut_type`; do not use
   `PL_LUT_UNKNOWN` for the calibration path.
5. Confirm the required high-level libplacebo attachment point with a neutral
   identity LUT and a strongly visible test LUT. If high-level frame attachment
   cannot guarantee the required late stage, implement a small renderer-native
   final pass using libplacebo's supported custom-LUT shader API rather than
   using a generic HLSL shader chain.
6. Ensure the post-LUT path does not run an implicit second gamma, range, or
   gamut conversion. Separate the *requested LUT reference target* from the
   DXGI swapchain signal negotiated/active on the desktop, and log both.
7. Cache the parsed LUT and GPU state by content signature. Reuse it for every
   frame. Reload only when the selected configuration/rule changes or when an
   explicit renderer rebuild is requested; live filesystem watching is out of
   scope.
8. On parse, validation, resource, or attachment failure:
   - continue playback with no LUT;
   - do not fail the GPU or renderer;
   - publish an accurate renderer detail/OSD state such as `LUT: unavailable`;
   - emit one clear actionable log entry and rate-limit repetitions.
9. Expose concise OSD/log diagnostics: LUT disabled/active/unavailable, file
   basename, LUT dimensions, selected display rule, reference primaries /
   transfer / range / nits, gamut-map mode, explicitly declared stage, content
   signature, requested reference target, and negotiated/active DXGI signal.
   When configured, show the descriptive output calibration profile separately.
   Do not claim the LUT is colorimetrically correct; that depends on how the
   user created it.
10. Free parser and GPU resources safely during rule replacement, reset, GPU
   reconstruction, and renderer shutdown. Coordinate all mutation with the
   alpha render-thread/render mutex; libplacebo renderer usage is not generally
   thread-safe.
11. Update `VideoProcessorRenderer.cfg` and `VideoProcessorRenderer.html` with
   examples, path rules, the target-space warning, HDR BT.2020-container/P3
   mastering explanation, gamut-map tradeoffs, and troubleshooting steps.

## Future 1D LUT extension

After calibrated 3D LUT support is validated, write a follow-up story for an
optional, ordered calibration chain: input 1D shaper, 3D LUT, then output 1D
shaper. Use libplacebo's native 1D `pl_custom_lut` representation and/or its
declared shaper support; do not create a VP-specific arbitrary shader stack.
Each stage will need an explicit input/output contract and diagnostics. The
initial story must keep its resource ownership and pipeline boundaries suitable
for that addition without exposing it prematurely.

## Verification

- Add parser/config tests for absent, valid identity, malformed, oversized, and
  missing `.cube` files; failed cases must retain normal no-LUT playback.
- Add deterministic rendering tests, or a GPU integration test where practical,
  showing that an identity LUT preserves output and a known test LUT transforms
  expected RGB sample values.
- Validate SDR Rec.709 input with a calibration LUT authored for the declared
  Rec.709 / BT.1886 (or other declared) reference range/transfer/nits.
- Validate PQ and HLG BT.2020 input tone mapped to P3-D65 / gamma 2.20 with a
  P3 / gamma 2.20 calibration LUT; confirm results are not equivalent to
  applying the LUT before tone mapping or after an unintended second gamma
  conversion.
- Use a gamut-stress test pattern/content outside P3 but inside BT.2020 to
  verify that the selected roll-off mode is visibly different from a clipping
  mode and that no early hard-coded BT.2020-to-P3 matrix conversion occurs.
- Switch source EOTF/colorspace and display rules repeatedly. Confirm LUT state
  follows the selected rule, no per-frame parse/reload occurs, and no renderer
  restart loop, queue drain, leak, or device failure results.
- Verify fullscreen/window transitions, F2/F3 screen-profile changes, refresh
  switching, renderer restart, missing optional alpha DLLs, and GPU recovery.

## Acceptance criteria

- Alpha applies a valid `.cube` 3D LUT at an explicitly documented
  output-calibration stage with an explicit LUT reference-target contract.
- HDR BT.2020 input is tone/gamut mapped to the declared LUT reference target
  before output calibration is applied; a P3 LUT does not cause premature source
  BT.2020-to-P3 clipping.
- The renderer does not apply an unrequested second transfer/range/gamut
  conversion after the calibration LUT.
- LUT selection is optional, rule-aware, cached, logged, and visible in concise
  renderer diagnostics.
- Invalid/missing LUTs degrade to normal no-LUT playback without disabling the
  renderer.
- Existing alpha output is bit-for-bit/functionally unchanged when no LUT is
  configured.
- The story is not Done until build/test evidence and calibrated real-display
  validation are recorded in this Status section.
