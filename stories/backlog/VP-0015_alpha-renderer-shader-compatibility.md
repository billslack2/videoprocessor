# VP-0015: Alpha renderer compatibility with the existing shader contract

## Status

Backlog — large.

Reassessed July 28, 2026 against the merged VP-0038/VP-0040 shader and viewport
work and the current Alpha/libplacebo pipeline.

The requested operator experience is feasible for the currently shipped NLS
shader family:

- use the existing `VideoProcessor.cfg`;
- use the existing `[shaders]` and `[shaders.<rule>]` sections unchanged;
- use the existing shortcuts unchanged;
- load the same `Shaders\NLS.hlsl` file named by `file`;
- add no Alpha-specific or backend-specific configuration keys; and
- make the Alpha result look the same as the established renderer for the same
  rule, parameters, active-picture geometry, and viewport.

This is not a small compatibility switch. The current `.hlsl` file is a
Direct3D 9/madVR external-pixel-shader program, while Alpha supplies planar
source textures to libplacebo and obtains RGB only inside libplacebo's render
pipeline. Libplacebo user hooks do not consume madVR HLSL directly. Supporting
the same file therefore requires a deliberately constrained compatibility
adapter or a new intermediate shader contract, plus Alpha shader lifecycle and
aspect-placement work.

The story is narrowed to the shader syntax and effects actually shipped by VP.
It does not promise that arbitrary third-party madVR HLSL will run in Alpha.

## Size and isolation assessment

| Requested result | Size | Assessment |
| --- | --- | --- |
| Reuse the current config sections and shortcuts | Small by itself | Shortcut discovery is already renderer-neutral and calls `IRenderer::SelectShaderRule`; Alpha must implement the existing renderer methods. |
| Share rule parsing, runtime selection, parameters, and trusted geometry | Medium | This state is still substantially owned by `MadVRShaderLoader` and must be extracted or safely shared without changing established-renderer behavior. |
| Run the same `NLS.hlsl` through Alpha at equivalent pre-resize semantics | Large part of the story | D3D9 HLSL cannot be handed to a libplacebo hook. A strict supported-subset adapter and hook owner are required. |
| Match NLS placement, safe fit, active-picture crop, and visual result | Medium to large | Alpha must coordinate the hook with its source crop and target aspect instead of madVR media-type negotiation. |
| Support arbitrary existing or future madVR HLSL | Unbounded/large | Explicitly out of scope. Shader interfaces, stages, resources, and color domains do not generally map one-to-one. |

Overall classification: **large**.

The implementation can be contained enough that the established renderer's
shader installation remains unchanged, but the work is **not fully
self-contained**. It necessarily touches:

- the shared renderer shader-selection interface and runtime state;
- shader configuration parsing currently located in `MadVRShaderLoader`;
- Alpha's render parameters, crop/aspect placement, shader cache, and
  GPU/rebuild lifecycle; and
- OSD/diagnostic reporting of the active rule.

The main regression risk is Alpha output, GPU recovery, frame pacing, and
renderer switching. Risk to madVR can be kept low by leaving
`IMadVRExternalPixelShaders` installation intact and characterizing the shared
parser/runtime behavior with tests before moving it. The shared shortcut path
and requested-rule persistence create a smaller cross-renderer regression
surface.

If the requirement were relaxed to allow a separate Alpha-native shader
implementation hidden in the executable, while keeping the same config and
hotkeys, the work would be medium. Requiring the named `.hlsl` file itself to
remain the authoritative implementation is what moves this story to large.

## User story

As an Alpha-renderer user, I want the existing VP shader rules to behave the
same after I change renderers, without maintaining a second configuration or a
second shader file.

For the current VP catalog, Shift+N, Shift+P, and plain N continue to select
classic NLS, protected-centre NLS, and NLS Off. A renderer switch preserves the
requested rule. Alpha reads the same rule and the same `Shaders\NLS.hlsl` as
the established renderer and produces a visually equivalent mapping.

## Current implementation evidence

The current state already provides useful shared pieces:

- `VideoProcessorDlg::CreateConfiguredAccelerators` reads shader shortcuts from
  the unified main configuration without regard to the selected renderer.
- `VideoProcessorDlg::OnCommandShaderRule` calls the existing
  `IRenderer::SelectShaderRule` contract.
- VP-0038 simplified each effect to one `file` and `stage`, grouped effects
  sharing a shortcut, and published the selected viewport and
  `$screen_aspect`.
- VP-0040 established trusted active-picture geometry, stable NLS engagement,
  safe-fit behavior, and renderer-rebuild ordering.
- `Shaders\NLS.hlsl` contains the current classic/protected NLS mapping,
  active-rectangle sampling, safe fit, and quality variants.

The missing pieces are material:

- only the DirectShow renderer overrides shader selection and refresh;
- rule parsing, parameter substitution, NLS resolution, and requested/effective
  runtime state remain coupled to `MadVRShaderLoader`;
- `MadVRShaderLoader` installs source through
  `IMadVRExternalPixelShaders` at madVR pre/post-resize stages;
- Alpha uploads planar Y and UV textures and lets libplacebo perform RGB/color
  conversion internally;
- Alpha has no shader-rule owner, no active shader list, and no corresponding
  `SelectShaderRule` or `RefreshShaderRule` implementation; and
- libplacebo's supported custom hooks use its hook/signature model and shader
  generator. They do not compile the current madVR D3D9 HLSL ABI as-is.

## Required compatibility contract

### Configuration and controls

Do not add, rename, or reinterpret configuration settings for Alpha.

Alpha must consume the existing keys exactly as the established renderer does,
including:

- `[shaders] enabled`, `rules`, and `default`;
- rule `shortcut`, `type`, `file`, `stage`, `none`, and label;
- NLS `geometry`, `strength`, `center_protection`, `curve`, `quality`, and
  `tolerance_percent`; and
- the existing runtime viewport, `$screen_aspect`, trusted active rectangle,
  safe-fit, and mapping values.

There must be no `alpha_file`, backend selector, Alpha shader directory,
duplicate rule, or second shortcut.

### Same-file authority

The file named by the existing `file` key is authoritative for both renderers.
Alpha must read that same file. It is not sufficient to recognize
`type: nls` and silently run a separately hard-coded copy of the algorithm.

For the initial implementation, define and test a strict compatibility subset
covering the constructs used by the shipped `NLS.hlsl`. The adapter may
translate those constructs into a libplacebo hook at load time. Unsupported
syntax, resources, stages, or entry-point contracts must reject the effect with
a clear diagnostic while Alpha continues rendering.

Do not attempt a general HLSL compiler or silently accept arbitrary madVR
shaders.

### Stage and image contract

`stage: pre_resize` must mean the same useful operation in both renderers:

1. operate on RGB video after source decoding/color representation is known;
2. apply the active-picture crop and the NLS coordinate mapping before the
   final viewport resize;
3. preserve Alpha's established color-management and display-LUT order; and
4. stretch or safe-fit to the selected viewport using the same resolved
   geometry as the established renderer.

The implementation must explicitly document the libplacebo hook chosen for
this contract. A final-output post-process that merely happens to load the
same file does not meet the story.

### Result equivalence

“Looks the same” means the two renderers produce the same geometric mapping,
protected-centre behavior, active-picture crop, safe-fit decision, and selected
quality mode on deterministic patterns and representative video.

Bit-for-bit equality is not required because madVR and libplacebo use different
scalers, color pipelines, precision, and shader compiler backends. Differences
must not include visible changes in face/centre geometry, bar removal, edge
position, mapping direction, or unexpected blur/ringing attributable to using
the wrong stage or sampling footprint.

## Implementation plan

1. **Freeze the shared behavior**
   - Add tests for current rule parsing, grouped shortcuts, requested/effective
     rule state, placeholder values, NLS activation, safe fit, and NLS Off.
   - Record deterministic established-renderer reference images for classic,
     protected, vertical, and safe-fit mappings at each quality mode.
2. **Extract the renderer-neutral rule model**
   - Move configuration parsing, normalized rule data, parameter evaluation,
     and requested/effective selection state out of the madVR installation
     class.
   - Keep the existing madVR adapter responsible only for compiling/installing
     the resolved chain through `IMadVRExternalPixelShaders`.
   - Ensure renderer replacement restores the requested rule without replaying
     a stale effective state.
3. **Prove the same-file adapter**
   - Define the minimal HLSL grammar/rewrites required by the shipped
     `NLS.hlsl`, including scalar/vector types, `tex2D`, derivatives, sampling,
     flow control, and VP placeholder substitution.
   - Translate the resolved source to a libplacebo custom hook and fail closed
     on every unsupported construct.
   - Add parser/translation tests and compare the generated mapping against a
     CPU reference, not only by visual inspection.
4. **Add the Alpha shader owner**
   - Implement Alpha `SelectShaderRule`, `RefreshShaderRule`,
     `ActiveShaderRule`, and `ActiveShaders`.
   - Create, cache, replace, and destroy hooks under the existing Alpha render
     and GPU lifetime. Shader failure disables only the effect and must not
     starve the frame queue or force a restart loop.
   - Recompile only when resolved shader source or compile-time parameters
     change; do not compile once per frame.
5. **Coordinate NLS geometry with Alpha**
   - Apply the hook at the proven pre-resize RGB stage.
   - Use the same trusted active rectangle, viewport aspect, mapping axis,
     stretch ratio, and safe-fit decision.
   - Update Alpha source/target rectangles without disturbing scope subtitle
     placement, display profiles, color transforms, or the display LUT.
6. **Validate lifecycle and performance**
   - Test startup in Alpha, madVR-to-Alpha and Alpha-to-madVR switches, repeated
     rule hotkeys, viewport changes, active-picture transitions, shader edit
     and reload behavior if supported, GPU recovery, and missing/invalid files.
   - Measure shader compile/cache time and 4K render cost at 23.976/24 and
     59.94/60 Hz.

## Non-goals

- No new configuration keys or Alpha-specific shader selections.
- No duplicate Alpha shader file.
- No promise of compatibility with arbitrary downloaded madVR HLSL.
- No change to established-renderer stage ordering or shader installation.
- No hard-coded Alpha NLS implementation that ignores edits to the configured
  HLSL source.
- No claim of pixel identity between renderer backends.
- No shader compilation or filesystem work while holding detector or capture
  queue locks.

## Verification

- The unchanged shipped configuration produces the same three shortcuts in
  both renderers.
- Shift+N, Shift+P, and plain N work before and after renderer switches and do
  not require restarting VP.
- Both renderers log the same configured rule, file, resolved parameters,
  trusted geometry revision, mapping axis, stretch ratio, and safe-fit state.
- Editing a supported expression in the single configured `NLS.hlsl`, then
  performing the supported reload/rebuild action, changes both renderer
  implementations. This proves Alpha is not running a hidden native copy.
- Deterministic grid, circle, face-position, letterbox, and pillarbox patterns
  show equivalent geometry for classic/protected horizontal NLS, vertical NLS,
  and safe fit.
- Validate SDR Rec.709, SDR BT.2020, HDR-to-SDR, and LLDV-style inputs with
  normal and scope viewports. Alpha's output color contract and display LUT
  remain unchanged apart from the intended spatial effect.
- Invalid HLSL, an unsupported construct, a missing file, or hook compilation
  failure leaves Alpha rendering normally, reports the reason, and shows the
  effect as inactive.
- Repeated toggles and at least 25 madVR/Alpha handoffs cause no crash, restart
  loop, stale rule, queue starvation, unexpected frame drops, or GPU-resource
  leak.

## Acceptance criteria

- The existing configuration and shader file are used without modification to
  their schema and without backend-specific additions.
- Alpha honors the existing shader hotkeys and preserves the requested rule
  across renderer switches.
- The same `Shaders\NLS.hlsl` is the authoritative source for both backends.
- The shipped NLS file passes the documented constrained adapter; unsupported
  files fail safely and visibly.
- Alpha applies NLS at equivalent pre-resize semantics and matches the
  established renderer's geometry and safe-fit behavior to the documented
  visual tolerance.
- Established-renderer shader behavior and VP-0038/VP-0040 state transitions
  remain unchanged.
- Alpha frame pacing, color management, display-LUT behavior, GPU recovery,
  and renderer handoff pass the regression matrix.

## References

- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`
- `src\VideoProcessor-Lib\IRenderer.h`
- `src\VideoProcessor-Lib\microsoft_directshow\MadVRShaderLoader.cpp`
- `src\VideoProcessor-Lib\microsoft_directshow\MadVRExternalPixelShaders.h`
- `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`
- `3rdparty\libplacebo\include\libplacebo\shaders\custom.h`
- `Shaders\NLS.hlsl`
- VP-0038: Generic viewport contract and aspect-driven NLS
- VP-0040: Trusted active-picture detection and stable NLS engagement
