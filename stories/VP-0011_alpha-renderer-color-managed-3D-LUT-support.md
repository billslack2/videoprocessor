# VP-0011: Alpha renderer color-managed 3D LUT support

## Status

Planned.

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
gamut maps it into the configured SDR target. The calibration LUT must be
applied at the **output-calibration stage**, after those operations. Applying a
projector SDR LUT directly to PQ/HLG/BT.2020 source values before tone mapping
is incorrect.

Do not rely only on `.cube` metadata: libplacebo treats LUT input/output
metadata as informative and does not automatically guarantee the desired
conversion order. VP must explicitly select and document the LUT stage.

## Scope

Support static `.cube` 3D LUT files for the alpha renderer only. The initial
feature applies one selected output-calibration LUT to an explicitly known
render target, normally SDR Rec.709 with the renderer's negotiated output range
and transfer behavior.

Use the existing `VideoProcessorRenderer.cfg` and display-rule machinery to
select a LUT at renderer initialization and source-state/rule changes.

## Non-goals

- Do not add LUT support to DirectShow/external renderers.
- Do not implement arbitrary user shader/LUT ordering or LUT stacks.
- Do not treat a calibration LUT as a substitute for correct input metadata,
  SDR target nits, gamut mapping, display range, or output transfer settings.
- Do not automatically apply arbitrary LUTs based solely on embedded `.cube`
  metadata.
- Do not reread, parse, allocate, or upload a LUT for every video frame.
- Do not add ICC-profile support in this story; libplacebo has separate ICC APIs
  and that deserves its own design if wanted later.

## Configuration design

1. Add a disabled-by-default base setting, for example:

   ```ini
   [libplacebo]
   output_lut_file=
   ```

   An empty value means no LUT.
2. Add an optional display-rule override using the same rule-selection lifecycle
   already used for alpha display settings. A rule-specific value replaces the
   base LUT only when that rule wins. Example intent:

   ```ini
   [display.sdr_projector]
   rule=$eotf==SDR
   priority=100
   output_lut_file=LUTs\Projector-SDR-Rec709.cube

   [display.hdr_to_sdr_projector]
   rule=$eotf==PQ|HLG|HDR
   priority=100
   output_lut_file=LUTs\Projector-HDR-to-SDR-Rec709.cube
   ```

3. Resolve relative paths against the executable/configuration directory, reject
   directory traversal outside that root unless an absolute path was explicitly
   configured, and log the resolved path without logging file contents.
4. Document that a LUT must be authored for the renderer's actual output target.
   Include a clear warning that a calibration LUT for limited RGB must not be
   used while the renderer is operating full RGB, and vice versa.

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
   - load the file once with a bounded file-size limit;
   - parse it with `pl_lut_parse_cube`;
   - validate that it is a three-dimensional LUT and its dimensions are within
     supported/resource-safe limits;
   - retain it until replacement, renderer reset, or destruction.
3. Attach the parsed LUT to the correct libplacebo render stage so it operates
   on the final intended SDR output, after input decoding, HDR-to-SDR tone
   mapping, and gamut mapping. Use an explicit `pl_lut_type`; do not use
   `PL_LUT_UNKNOWN` for the calibration path.
4. Confirm the required high-level libplacebo attachment point with a neutral
   identity LUT and a strongly visible test LUT. If high-level frame attachment
   cannot guarantee the required late stage, implement a small renderer-native
   final pass using libplacebo's supported custom-LUT shader API rather than
   using a generic HLSL shader chain.
5. Cache the parsed LUT and GPU state by content signature. Reuse it for every
   frame. Reload only when the selected configuration/rule changes or when an
   explicit renderer rebuild is requested; live filesystem watching is out of
   scope.
6. On parse, validation, resource, or attachment failure:
   - continue playback with no LUT;
   - do not fail the GPU or renderer;
   - publish an accurate renderer detail/OSD state such as `LUT: unavailable`;
   - emit one clear actionable log entry and rate-limit repetitions.
7. Expose concise OSD/log diagnostics: LUT disabled/active/unavailable, file
   basename, LUT dimensions, selected display rule, explicitly declared stage,
   and content signature. Do not claim the LUT is colorimetrically correct;
   that depends on how the user created it.
8. Free parser and GPU resources safely during rule replacement, reset, GPU
   reconstruction, and renderer shutdown. Coordinate all mutation with the
   alpha render-thread/render mutex; libplacebo renderer usage is not generally
   thread-safe.
9. Update `VideoProcessorRenderer.cfg` and `VideoProcessorRenderer.html` with
   examples, path rules, the target-space warning, and troubleshooting steps.

## Verification

- Add parser/config tests for absent, valid identity, malformed, oversized, and
  missing `.cube` files; failed cases must retain normal no-LUT playback.
- Add deterministic rendering tests, or a GPU integration test where practical,
  showing that an identity LUT preserves output and a known test LUT transforms
  expected RGB sample values.
- Validate SDR Rec.709 input with a calibration LUT authored for the configured
  output range/transfer.
- Validate PQ and HLG input tone mapped to SDR with a LUT explicitly authored
  for that final SDR target; confirm results are not equivalent to applying the
  LUT before tone mapping.
- Switch source EOTF/colorspace and display rules repeatedly. Confirm LUT state
  follows the selected rule, no per-frame parse/reload occurs, and no renderer
  restart loop, queue drain, leak, or device failure results.
- Verify fullscreen/window transitions, F2/F3 screen-profile changes, refresh
  switching, renderer restart, missing optional alpha DLLs, and GPU recovery.

## Acceptance criteria

- Alpha applies a valid `.cube` 3D LUT at an explicitly documented
  output-calibration stage.
- HDR input is tone/gamut mapped before output calibration is applied.
- LUT selection is optional, rule-aware, cached, logged, and visible in concise
  renderer diagnostics.
- Invalid/missing LUTs degrade to normal no-LUT playback without disabling the
  renderer.
- Existing alpha output is bit-for-bit/functionally unchanged when no LUT is
  configured.
- The story is not Complete until build/test evidence and calibrated real-display
  validation are recorded in this Status section.
