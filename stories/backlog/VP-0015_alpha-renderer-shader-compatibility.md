# VP-0015: Alpha renderer shader compatibility with the existing shader catalog

## Status

Backlog. Feasible as a compatibility feature, but arbitrary existing madVR HLSL
must not be assumed to run unchanged in the alpha renderer. Complete the
capability inventory and shader-stage spike before moving this story to
In Progress.

## User story

As an alpha-renderer user, I want the useful shader effects I can configure for
the established renderer to be available when the alpha renderer is selected,
so changing renderers does not unexpectedly remove sharpening, denoising,
aspect-ratio/NLS processing, or other configured video processing.

## Feasibility decision

This is possible, but the safe implementation is a renderer-neutral profile
model plus libplacebo-native shader implementations or a deliberately limited
translation layer. It is not safe to pass every existing HLSL file directly to
libplacebo:

- the current loader sends source text, a D3D shader profile, a stage, and
  madVR-specific parameters through `IMadVRExternalPixelShaders`;
- libplacebo custom shaders use its hook/signature/resource model and are
  compiled through libplacebo's GPU backend; its public shader-hook work was
  initially designed around mpv-style hooks, not the madVR external-shader
  interface;
- stage names, coordinate conventions, available textures, color-pipeline
  placement, parameter binding, and compilation/runtime errors therefore do not
  have a general one-to-one mapping.

The first implementation target should be native libplacebo ports of the
small, useful subset of effects. A source translator is optional research, not
an acceptance criterion. If a shader cannot be mapped with equivalent inputs
and stage semantics, it must be reported as unsupported and skipped safely.

## Scope

Alpha/libplacebo renderer only, while preserving the current external-shader
path for the established renderer. The work includes:

- an inventory and compatibility matrix for every shipped/configurable shader;
- a renderer-neutral selection/profile representation that can choose an
  implementation per backend without making the existing config ambiguous;
- libplacebo hook/pass integration with explicit pre-scale, scaled/post-scale,
  and final-output semantics where the backend can support them;
- initial native ports of the highest-value effects, starting with sharpen and
  denoise and then evaluating NLS/aspect processing separately;
- profile conditions for source transfer/classification, nominal rate and
  active screen profile, using metadata already available to VP;
- asynchronous compilation/prewarm and persistent caching where supported;
- OSD/log reporting of selected, active, unsupported, failed, and fallback
  shaders; and updated configuration/help examples.

The default remains no alpha shader. Existing established-renderer behavior,
shader files, stage ordering, and failure handling must remain unchanged.

## Current evidence to preserve

The current implementation should be treated as two contracts, not one:

- `src\\VideoProcessor-Lib\\microsoft_directshow\\MadVRShaderLoader.cpp`
  loads configured files and installs them by external-renderer stage through
  `IMadVRExternalPixelShaders`.
- `src\\VideoProcessor-Lib\\libplacebo\\LibplaceboVideoRenderer.cpp` owns
  the alpha renderer's libplacebo render pipeline, GPU lifetime, frame queue,
  screen profiles, color management, and persistent shader cache.

The existing configured shader names and user intent may be shared. The shader
source/ABI may not be shared unless a compatibility test proves it is valid for
both backends.

## Required capability matrix

Before implementation, classify each configured effect as:

| Classification | Meaning | Required behavior |
| --- | --- | --- |
| Native port | Rewritten for libplacebo hooks/passes with equivalent behavior | Can be enabled after visual/performance validation |
| Constrained translation | A documented source subset can be translated or adapted | Must reject unsupported syntax/resources and log the reason |
| Backend-specific | Depends on madVR stages, interfaces, or unavailable state | Remains unavailable on alpha; no silent substitution |
| Not yet classified | Unknown semantics or insufficient test vectors | Block production use until the spike resolves it |

At minimum evaluate all currently shipped/configured sharpen, denoise,
adaptive-sharpen, NLS/aspect, and utility shaders. Record input/output color
domain, required textures, scale/viewport assumptions, parameters, stage,
temporal state, and expected cost.

## Implementation plan

1. **Inventory and stage spike**
   - Read the current shader config and loader rules, including substitutions
     such as active rectangle and screen-profile values.
   - Create representative test shaders for a simple point operation, a sampled
     spatial filter, a multi-pass effect, and NLS geometry/aspect behavior.
   - Map each madVR stage to the nearest libplacebo hook and document cases
     where no equivalent exists. Decide whether a constrained adapter is worth
     maintaining; otherwise choose native ports.
2. **Renderer-neutral profile contract**
   - Preserve the current easy-to-configure profile/rule selection model.
   - Add explicit backend implementation metadata or a per-backend file entry;
     never infer that an HLSL file is valid for libplacebo from its filename.
   - Define whether each effect runs before scaling, after scaling, or at final
     output, and expose the input/output rectangle and color-domain contract.
3. **Libplacebo integration**
   - Add a shader-chain owner with clear create/rebuild/destroy lifetime tied to
     the alpha renderer, including GPU reset and display/screen-profile changes.
   - Use libplacebo's hook/custom-shader API and bundled version constraints;
     do not bypass its resource/signature validation.
   - Compile/prewarm outside the frame-critical path when possible. A compile
     error, missing file, unsupported hook, or GPU failure must disable only
     that effect, keep rendering, and leave queues fed.
4. **Initial ports and dynamic rules**
   - Port sharpen and denoise first, with controls and profiles for SDR 59/60 Hz
     as the initial practical use case.
   - Evaluate NLS as a native effect with explicit active-picture/viewport data;
     do not assume madVR template substitutions can be copied.
   - Re-evaluate shader selection only on a stable metadata/profile transition,
     not once per frame. Do not reset the renderer for a shader that can be
     rebuilt at a safe boundary.
5. **Diagnostics and documentation**
   - Log profile, backend, source file/implementation, stage, compile/cache
     result, activation time, disable reason, and fallback behavior.
   - Show active shader names in the alpha OSD without claiming an effect is
     active before its chain is installed.
   - Document supported shader implementations, parameters, performance cost,
     and the fact that an existing HLSL file may require a native port.

## Verification

- With no shaders configured, compare alpha output, timing, color metadata,
  queue health, and renderer lifecycle to the current baseline.
- Validate each initial port with deterministic input patterns and visual
  reference captures against the established renderer where comparable; exact
  pixel identity is not required if the intended algorithm differs, but the
  difference must be documented.
- Test SDR Rec.709, SDR BT.2020, HDR-to-SDR, LLDV-style input, 23.976/24 and
  59.94/60 Hz, normal/scope profiles, 16:9 and 4:3 content, and renderer/GPU
  recovery.
- Exercise profile changes and shader compile failures while playing. Confirm
  no queue starvation, unexplained frame drops, restart loop, or crash.
- Confirm an unsupported madVR-only shader produces a clear diagnostic and
  leaves the alpha renderer usable.

## Acceptance criteria

- The capability matrix and the native-versus-translation decision are checked
  in before production shader ports.
- Alpha can activate at least the agreed initial native shader set through the
  normal profile/rule UI without changing the established renderer path.
- Shader stages, color domain, viewport, parameters, and backend are explicit;
  no unsupported madVR HLSL is silently treated as alpha-compatible.
- Compilation/cache failures are isolated to the affected shader and cannot
  starve or crash the renderer.
- OSD/logs and HTML/config documentation accurately show what is active,
  unavailable, or disabled and why.

## References

- Current VP shader loader:
  `src\\VideoProcessor-Lib\\microsoft_directshow\\MadVRShaderLoader.cpp`
- Current alpha renderer:
  `src\\VideoProcessor-Lib\\libplacebo\\LibplaceboVideoRenderer.cpp`
- [libplacebo custom shader-hook design](https://code.videolan.org/videolan/libplacebo/-/merge_requests/86)
- [libplacebo shader-hook API discussion](https://code.videolan.org/videolan/libplacebo/-/issues/19)
- [libplacebo release notes describing mpv-style user shaders](https://code.videolan.org/videolan/libplacebo/-/tags/v2.72.0-rc2)
