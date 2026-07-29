# VP-0051: Generic Alpha shader-chain support

## Status

Backlog. No implementation has started.

This story builds on the Alpha hook ownership and renderer-neutral selection
work proven by VP-0015. VP-0015 must complete its production review, or its
reusable hook-lifetime and selection contracts must be explicitly accepted,
before this story changes that path.

## User story

As an Alpha-renderer user, I want VP to load useful general-purpose
libplacebo/mpv-style GLSL shader chains rather than accepting only typed NLS
hooks, so compatible sharpening, denoising, debanding, scaling, color, and
other spatial effects can be selected and applied without becoming dead
packaged assets.

## Problem

Alpha currently has a specialized path for a typed NLS GLSL hook. That proves
that VP can parse and install an mpv user shader, but it is not a generic
shader-chain contract:

- non-NLS shader rules are not treated as Alpha-compatible chains;
- ordered multi-shader execution and stage placement are not generally
  modeled;
- shader capabilities and unsupported directives are not clearly classified;
- parameters, textures, intermediate outputs, and cache ownership need a
  renderer-safe lifetime;
- the exact placement relative to scaling, tone mapping, gamut conversion,
  LUTs, dithering, and overlays is not defined; and
- a file being named `.glsl` or `.hook` does not by itself prove that VP's
  Alpha pipeline can execute it correctly.

Until this is implemented, generic Alpha shader files must not be documented
or packaged as usable merely because libplacebo can parse some user hooks.

## Scope boundary

This story supports renderer-native mpv/libplacebo user shaders. It does not
make Alpha execute madVR HLSL directly and does not promise compatibility with
every shader written for MPC, madVR, mpv, or another player.

The implementation must publish a precise supported subset based on the
libplacebo version bundled with VP and Alpha's actual render pipeline.
Unsupported shader semantics must fail validation clearly and nonfatally.

Temporal effects that require previous/next decoded frames, motion vectors,
optical flow, or a player-managed history buffer are out of scope unless the
initial capability spike proves that the existing libplacebo hook contract
provides everything required without introducing new frame-retention or
cadence behavior. Such effects must never be silently treated as ordinary
single-frame hooks.

## Required capability spike

Before finalizing configuration or implementation, inventory and test the
bundled libplacebo user-shader API:

1. accepted mpv user-shader syntax and hook points;
2. multiple passes and ordered hooks;
3. `BIND`, `SAVE`, intermediate textures, and named texture dependencies;
4. user parameters and their supported scalar/vector types;
5. texture declarations and external resource requirements;
6. compute-shader directives, workgroup requirements, and GPU capability
   checks;
7. source-size, target-size, component, color, and frame-related variables;
8. resizing behavior and the distinction between hooks that replace scaling
   and hooks that filter an already-sized image;
9. frame history or temporal semantics, if any; and
10. parse-time versus GPU-compile-time failure behavior.

For each feature, record **Supported**, **Unsupported**, or **Deferred**, with
an executable fixture and the reason. The supported subset becomes the public
Alpha shader contract and must be tied to the bundled libplacebo version.

## Configuration design

Reuse the existing shared `[shaders]` rule selection, EOTF/frame-rate matching,
shortcut grouping, default behavior, and OSD reporting. Do not introduce a
second Alpha-only configuration file.

Extend a canonical rule so it can describe an ordered Alpha-native chain. The
final schema must be settled with VP-0045 and documented by VP-0049, but should
express these concepts without backend-specific duplicate keys:

- one or more ordered shader files;
- renderer/source compatibility;
- a supported pipeline stage or hook-derived automatic placement;
- optional named parameter overrides;
- rule conditions already supported by VP; and
- an explicit empty/off rule.

A possible shape for design review is:

```ini
[shaders.alpha_broadcast]
label: Alpha Broadcast Cleanup
files: Denoise.glsl,AdaptiveSharpen.glsl
stage: AUTO
eotf: SDR
rates: 59,60
```

This is illustrative, not approved syntax. The implementation must first
decide whether repeated keys, a list value, or ordered child entries best fit
the canonical parser. It must not overload the existing single `file` field in
a way that changes established madVR behavior.

`AUTO` stage placement, if supported, must have a deterministic documented
meaning. It cannot mean "try somewhere until the shader compiles."

## Pipeline contract

Define and test where each supported hook runs relative to:

1. input unpacking and YUV-to-RGB conversion;
2. source crop and trusted active-picture geometry;
3. NLS or other geometry mapping;
4. chroma and luma scaling;
5. tone mapping and gamut mapping;
6. display transform and 3D/1D LUT processing;
7. output transfer, range conversion, and final dithering; and
8. subtitles and the native OSD.

Each shader must receive truthful dimensions, color representation, transfer
state, and component semantics for its stage. VP must not expose a generic
`pre_resize` or `post_resize` label unless it maps consistently to an actual
libplacebo hook point.

Ordering must be deterministic. If one shader saves an intermediate texture
that a later shader binds, dependency order must be validated before renderer
installation.

## Runtime and lifetime requirements

- Parse and validate a requested chain before replacing the active chain.
- Own parsed hooks, parameters, textures, and compiled GPU resources under the
  existing Alpha renderer/GPU generation and synchronization contract.
- Atomically activate all members of a chain; never leave a partially
  installed chain after one member fails.
- Keep the last known-good chain active when a replacement cannot be loaded,
  unless the user explicitly selects the off rule.
- Cache compiled artifacts using content, parameters, libplacebo version, GPU,
  driver, pipeline stage, and relevant render-contract inputs as cache keys.
- Invalidate resources safely on device loss, renderer replacement, shader
  file change, or incompatible output-contract change.
- Do not restart the renderer merely to select, clear, or replace a compatible
  spatial chain.
- Shader parsing or compilation must not block frame delivery long enough to
  starve the Alpha queue. Prepare asynchronously or outside the presentation
  critical section and commit the completed chain atomically.

## Capability and failure reporting

At configuration load or rule selection, distinguish:

- compatible and active;
- compatible but waiting for asynchronous compilation;
- not applicable to Alpha;
- unsupported hook feature;
- invalid configuration or parameter;
- parse failure;
- GPU compilation/resource failure; and
- retained previous chain after replacement failure.

Logs must name the rule, ordered file, failing directive or stage, and fallback
action without dumping full shader source. The OSD must show the effective
rule and active shaders in execution order, not merely the requested rule.

An unsupported shader must not crash Alpha, stall capture, drain the queue,
trigger a renderer-reset loop, alter cadence, or make video disappear.

## Initial supported-use matrix

The spike must evaluate at least:

- single-pass sharpening;
- single-pass spatial denoising;
- debanding or grain/dither-like processing;
- edge or detail enhancement;
- color adjustment that is safe at a defined pipeline stage;
- multi-pass chain with an intermediate texture;
- resize-aware hook;
- compute hook, whether supported or deliberately rejected; and
- a temporal/history-dependent shader that must either be proven supported or
  rejected with a precise reason.

Choose small redistributable test shaders with verified licenses. Do not add a
shader to release assets until its Alpha compatibility, pipeline placement,
license, and configuration example are validated.

## Compatibility and isolation

- Preserve the current paired NLS behavior and trusted geometry contract from
  VP-0015.
- Preserve existing madVR HLSL loading, parameter substitution, stage
  installation, and chain ordering.
- Keep renderer compatibility resolution explicit when one shortcut group has
  backend-specific members.
- Do not infer compatibility solely from a filename extension.
- Do not change queue depth, frame timestamps, cadence correction, refresh
  switching, color-output contracts, LUT behavior, subtitle placement, or OSD
  placement as a side effect.

## Verification

### Automated

- Capability fixtures cover every advertised supported directive and every
  deliberately rejected category.
- One and multiple compatible hooks parse, compile, install, execute in order,
  and release exactly once per GPU generation.
- Conditional SDR/HDR and nominal-rate rules resolve using existing shared
  rule logic.
- Parameter overrides produce deterministic cache keys and effective values.
- Replacement is atomic and a failed replacement retains the last known-good
  chain.
- Explicit off clears the complete VP-managed Alpha chain.
- Unsupported temporal, compute, texture, stage, or dependency behavior is
  rejected before it can disturb rendering.
- Switching Alpha to madVR and back restores each renderer's compatible
  effective member without cross-installing GLSL or HLSL.
- Existing NLS, color, LUT, queue, cadence, renderer-switch, and shader tests
  remain green.

### Runtime

- Validate representative sharpening, denoising, debanding, color, resize,
  and multi-pass fixtures at 1080p and 2160p, SDR and HDR input, and 23/24 and
  59/60 Hz where applicable.
- Confirm execution order visually and through deterministic test patterns or
  image checks, not only successful compilation.
- Change rules repeatedly during playback without renderer restart, queue
  starvation, stale frames, or dropped-frame bursts.
- Exercise cold and warm shader caches, device loss/rebuild, fullscreen and
  windowed operation, refresh switching, and repeated Alpha/madVR handoffs.
- Build and run the full x64 Release test suite.

## Documentation

After implementation, VP-0049 must document:

- the exact Alpha-supported shader subset;
- compatible file syntax and pipeline stages;
- ordered-chain and parameter syntax;
- the exact meaning of `AUTO`;
- unsupported temporal/backend-specific categories;
- performance and first-compilation behavior;
- effective-chain OSD/log reporting; and
- complete validated examples.

Do not advertise generic files in the sample configuration or release package
before these requirements pass.

## Acceptance criteria

- Alpha can execute a validated ordered chain of supported, non-NLS
  mpv/libplacebo GLSL hooks.
- The supported shader feature subset and stage mapping are explicit, tested,
  and versioned.
- Useful spatial sharpening, denoising, debanding/detail, color, resize-aware,
  and multi-pass examples are either supported or have a documented technical
  reason for exclusion.
- Unsupported shaders fail clearly and nonfatally; no file is assumed
  compatible only because it has a GLSL-like extension.
- Chain replacement is atomic, cache-safe, renderer-restart-free, and does not
  starve frame delivery.
- OSD and logs report the effective ordered chain and precise failure state.
- Existing Alpha NLS and madVR shader behavior do not regress.
- Canonical configuration and `CONFIGURATION.html` are updated only after the
  implemented contract is verified.

## References

- VP-0015: Alpha renderer support for paired shader rules
- VP-0045: Namespace built-in renderer configuration as `vpvr`
- VP-0049: Complete canonical `CONFIGURATION.html` reference
- `src\VideoProcessor-Lib\IRenderer.h`
- `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`
- `src\VideoProcessor-Lib\microsoft_directshow\MadVRShaderLoader.cpp`
- bundled libplacebo `pl_mpv_user_shader_parse` and custom-hook APIs
