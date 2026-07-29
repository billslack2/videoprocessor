# VP-0015: Alpha renderer support for paired shader rules

## Status

Done. Accepted on 2026-07-29 after the merged and deployed implementation was
confirmed present in `v1.1.015-beta`. The remaining long-run hardware matrix
is valuable regression coverage, but is no longer a release gate for this
accepted story.

Started July 28, 2026 from the current default VP integration branch
`v1.1.014-beta`.

Implemented in `codex/vp-0015-alpha-nls` and merged through PR `#20` as
`f1ae8a1`. After production validation, PR `#22` made the tested madVR
`quality: high` choice permanent in the checked-in classic and protected
profiles. Its integration merge `15577fc` was rebuilt cleanly in x64 Release,
passed 183/183 tests, and was deployed July 28, 2026. Current rollback is
`C:\Videoprocessor\vp\backup-before-vp0015-merged-high-20260728-193828`; the
earlier pilot rollbacks remain
`C:\Videoprocessor\vp\backup-before-vp0015-high-20260728-190516` and
`C:\Videoprocessor\vp\backup-before-vp0015-20260728-182854`.

The production pilot proves paired selector resolution, live Alpha GLSL NLS,
requested-state restoration across Alpha/madVR replacement, and the unchanged
madVR HLSL path. The story remains in Review rather than Done until the full
mixed-content hardware matrix and repeated-handoff soak below are completed.

## Implementation and deployment evidence

- Added packaged `Shaders\NLS.glsl` in mpv user-shader format and parsed it
  successfully with bundled libplacebo's `pl_mpv_user_shader_parse` API.
- Same-shortcut selectors classify `.hlsl` as madVR, `.glsl`/`.hook` as
  Alpha, and `none: true` as source-independent before installation.
- The GUI owns the durable complete selector across the optional DLL boundary;
  each newly built renderer resolves only its compatible member.
- Alpha implements `SelectShaderRule`, `RefreshShaderRule`,
  `ActiveShaderRule`, and `ActiveShaders`, owns hook lifetime under the
  renderer/GPU mutex, and keeps parse or hook failure nonfatal.
- Alpha reuses `ExtractP010ActivePictureEvidence`,
  `ActivePictureTransitionModel`, and `EvaluateMadVRNlsMapping`. It does not
  replace or modify Alpha's separate subtitle detector.
- Waiting/transition state ignores any older subtitle-detector crop and
  safe-fits the complete current raster until trusted NLS geometry returns.
- Source crop and target rectangles implement trusted Scope passthrough,
  nonlinear horizontal/vertical fill, and geometry-preserving safe fit without
  a renderer restart.
- Exact merge commits `f1ae8a1`, `6307696`, and final high-quality-default
  merge `15577fc` each passed a clean x64 Release rebuild and 183/183 native
  tests. Release output contained both `NLS.hlsl` and `NLS.glsl`.
- Production configuration was backed up and minimally changed: existing
  capture, madVR, broadcast, display, queue, and profile values were preserved;
  only two Alpha rule names and two GLSL rule sections were added.
- The July 28 high-quality madVR pilot changed only `quality: medium` to
  `quality: high` in `shaders.nls` and `shaders.nls_protected`, selecting the
  existing six-tap Lanczos-3-style HLSL reconstruction path.
- PR `#22` synchronized those two tested values into the source configuration
  and matching HTML examples. Final deployment preserved the active production
  configuration byte-for-byte, matched all rebuilt artifact hashes, and
  restarted successfully.
- Live production Alpha validation resolved `nls,nls_alpha` to
  `nls_alpha`, loaded only `NLS.glsl`, accepted trusted full-raster
  `3840x2160` geometry, and applied horizontal NLS from `1.7778` to `2.3500`
  with stretch `1.32188`.
- Switching back without pressing N again restored the same selector. madVR
  resolved only `nls`, loaded the byte-identical existing `NLS.hlsl`, changed
  picture aspect dynamically to `235:100`, and reported
  `renderer_restart=0`. Production was then left on madVR with `NLS: Off`.

## Known pilot caveat and remaining validation

The first uncached Alpha hook activation spent about 149 ms translating GLSL
to SPIR-V. With the deployed intentionally small `alpha_queue_size: 1`, that
one-time compile caused one hard overflow followed by one queue-only recovery.
Rendering stabilized immediately and the compiled cache was saved. This is
not a renderer restart, madVR regression, or recurring mapping failure, but it
does not yet satisfy the ideal no-queue-reset activation criterion.

Before moving to Done, field-check:

- trusted encoded Scope-bar removal;
- 1.85/1.90 and Disney+ Scope/IMAX transitions;
- 4:3 to normal 16:9 NLS and 4:3-to-Scope safe fit;
- Shift+P protected mode and plain-N off in Alpha;
- warm-cache activation behavior; and
- the planned repeated Alpha/madVR handoff soak.

## User story

As an Alpha-renderer user, I want Shift+N, Shift+P, and plain N to control NLS
as they do when madVR is selected. Alpha may use a different nonlinear mapping
and different shader parameters, but it must obey VP's established
active-picture and viewport policy:

- do not stretch 4:3 content directly to a 2.35:1 Scope viewport;
- preserve 4:3 with the existing safe-fit/pillarbox decision on Scope;
- allow eligible 4:3-to-16:9 NLS on the normal viewport;
- treat confirmed Scope content on a Scope viewport as linear passthrough after
  removing encoded bars;
- stretch eligible 16:9/1.85/1.90 IMAX-style scenes to the selected Scope
  viewport;
- follow confirmed Disney+/IMAX mixed-aspect transitions without a renderer
  restart; and
- retain the requested NLS state through Alpha/madVR handoffs and unrelated
  renderer replacement until the user selects NLS Off.

## Configuration design

Each renderer receives its own rule and shader source. Rules that share a
shortcut form one requested selection group:

```ini
[shaders]
enabled: true
rules: nls,nls_alpha,nls_protected,nls_protected_alpha,nls_off
default: none

[shaders.nls]
label: Nonlinear Stretch
shortcut: N
type: nls
file: NLS.hlsl
stage: pre_resize
geometry: classic
strength: 1.0
curve: 2.0
quality: high
tolerance_percent: 5

[shaders.nls_alpha]
label: Nonlinear Stretch
shortcut: N
type: nls
file: NLS.glsl
stage: pre_resize
# Alpha/mpv parameters may differ from the HLSL rule.

[shaders.nls_protected]
label: Nonlinear Stretch Protected
shortcut: P
type: nls
file: NLS.hlsl
stage: pre_resize
geometry: protected

[shaders.nls_protected_alpha]
label: Nonlinear Stretch Protected
shortcut: P
type: nls
file: NLS.glsl
stage: pre_resize
# Alpha/mpv parameters may differ from the HLSL rule.

[shaders.nls_off]
label: NLS Off
shortcut: n
output_aspect_ratio: native
none: true
```

The final parameter names and checked-in Alpha shader may be simplified after
the shader spike. Do not require the two shader implementations to expose the
same options or produce the same nonlinear curve.

### Source compatibility

Classify candidates before reading or compiling their source:

| Renderer | Compatible source |
| --- | --- |
| DirectShow/madVR | `.hlsl` |
| Alpha/libplacebo | mpv-style `.glsl` or `.hook` |
| Source-independent | `none: true` |

An incompatible candidate in the selected shortcut group is **not
applicable**, not a compilation failure. It is skipped without warning.
Missing compatible source, invalid stage, parser failure, or GPU compilation
failure is reported for the selected renderer and leaves video rendering
normally with that effect inactive.

Do not make duplicate `file` keys into arrays and do not add backend-specific
keys such as `mpv_file` or `alpha_file`.

## Shared state contract

The durable requested state is the complete shortcut group, for example
`nls,nls_alpha`, rather than whichever backend member was installed most
recently.

```text
Requested selection: Shift+N group
madVR active member: nls
Alpha active member: nls_alpha
```

On renderer replacement, resolve the saved group against the incoming
renderer and apply its compatible member. An automatic waiting, passthrough,
safe-fit, or failed state must not overwrite the durable user request. Plain N
remains the deliberate shared off request.

Keep requested group, effective backend member, mapping mode, shader install
state, and fallback/failure reason distinct in logs and OSD.

## Existing policy to reuse

The difficult content decisions are already implemented and accepted by
VP-0034, VP-0035, VP-0038, and VP-0040. Alpha must consume the same coherent
runtime inputs rather than adding a second detector or deciding content
eligibility inside GLSL:

- renderer-independent requested/effective NLS state;
- selected viewport and canonical `$screen_aspect`;
- trusted active-picture rectangle and source/media epoch;
- trusted and candidate generations;
- nonlinear, linear Scope passthrough, safe-fit, waiting, and off mapping
  modes;
- warp axis, source/target aspect, stretch ratio, and crop authority; and
- restart-free mapping revisions during mixed-aspect playback.

Only confirmed crop authority may crop encoded bars. Aspect-only confidence may
select a non-destructive mapping mode but must not invent exact source
coordinates.

## Shader selection

Research existing mpv-style nonlinear stretch shaders, including:

- NLS-Next;
- the older mpv `nonlinear_stretch.glsl`; and
- HyperView/SuperView-style hooks.

Choose the most suitable starting point for VP's bundled libplacebo version,
license, performance requirements, and parameter model. Existing code may be
adapted with attribution when its license is compatible. It need not reproduce
the HLSL curve, but VP must provide the trusted source rectangle, destination
aspect, safe mapping mode, and current renderer generation directly; do not
bring in an mpv Lua helper.

The expected Alpha integration is an mpv-style hook parsed through
`pl_mpv_user_shader_parse` and attached to `pl_render_params.hooks` at the
equivalent pre-resize RGB stage. Confirm the actual hook mapping with a
compile/render spike before production use.

## Implementation plan

1. Characterize existing grouped-shortcut behavior, durable NLS state, and
   madVR installs with focused tests.
2. Add renderer source-capability classification without changing the madVR
   handling of compatible `.hlsl` rules.
3. Move only the requested shortcut-group ownership and shared resolved NLS
   snapshot above the renderer-specific installers.
4. Implement Alpha `SelectShaderRule`, `RefreshShaderRule`,
   `ActiveShaderRule`, and `ActiveShaders`.
5. Add an Alpha shader owner that reads, parses, installs, caches, replaces,
   and destroys compatible mpv hooks under the existing render/GPU lifetime.
6. Feed Alpha the accepted trusted geometry and mapping mode. Coordinate its
   source/target rectangles without changing color management, display LUT,
   overlays, subtitle fitting, frame cadence, or queue behavior.
7. Add the chosen/adapted `Shaders\NLS.glsl`, configuration examples, HTML
   documentation, diagnostics, and focused tests.
8. Build and validate both renderers, deploy only clean x64 Release artifacts,
   and minimally edit the deployed configuration after a timestamped backup.

## Isolation assessment

This is not completely Alpha-local because:

- the GUI currently sends a grouped rule selector to the active renderer;
- durable NLS state and rule parsing remain coupled to `MadVRShaderLoader`; and
- renderer replacement must restore the logical group before choosing a
  backend member.

The necessary shared changes must be limited to selection ownership,
source-capability filtering, and publication of an already-resolved NLS
snapshot. The following must remain isolated:

- madVR COM interface usage;
- HLSL source loading and parameter substitution;
- madVR stage clear/install ordering;
- madVR output/media aspect behavior;
- DirectShow graph/restart/queue policy; and
- existing HLSL and configuration values for the madVR rules.

If implementation requires modifying any of those isolated areas, stop and
record why before proceeding.

## Verification

### Automated

- Existing full native test suite remains green.
- Existing VP-0034/VP-0035/VP-0038/VP-0040 mapping, detector, runtime-state,
  renderer-replacement, and madVR shader tests remain green without relaxed
  assertions.
- Same-shortcut HLSL/GLSL candidates resolve to exactly one compatible member
  per renderer.
- An incompatible member is Not Applicable and is never opened or compiled.
- Missing/invalid Alpha GLSL fails only that effect.
- Duplicate shortcut delivery is idempotent.
- Requested group persists while effective backend member changes.
- Alpha hook resources are released/rebuilt exactly once per renderer/GPU
  generation.

### Runtime and hardware

- Shift+N, Shift+P, and plain N work in Alpha and madVR.
- Switch madVR to Alpha and back while NLS is armed; each renderer restores its
  member without another key press.
- Validate normal 16:9, 4:3 pillarbox, Scope, 1.85/1.90 IMAX, Disney+
  Scope/IMAX transitions, and 4:3 on a Scope viewport.
- Scope passthrough removes only trusted encoded bars.
- 4:3 on Scope remains safe-fit/pillarboxed and is never stretched to 2.35.
- Mixed-aspect transitions update without renderer restart, queue reset, stale
  crop, or visible output-contract flash.
- Alpha color output, display LUT, OSD, subtitle placement, cadence, queue
  depth, dropped-frame count, GPU recovery, and windowed/fullscreen
  presentation remain unchanged apart from the selected spatial effect.
- Repeat at least 25 Alpha/madVR handoffs with NLS armed.

## Acceptance criteria

- Paired rules with one shortcut provide renderer-specific HLSL and mpv-style
  GLSL implementations.
- The requested shortcut group is durable and renderer-independent.
- Alpha uses the same accepted trusted-picture and viewport policy as madVR,
  including Disney+/IMAX transitions and 4:3 safe fit on Scope.
- Alpha shader failure cannot crash, starve, or restart the renderer.
- No madVR HLSL loading, substitution, stage installation, output-contract, or
  runtime mapping behavior changes.
- Clean x64 Release GUI, Alpha plugin, and native test builds pass.
- Production deployment preserves existing user configuration except for the
  smallest required paired-rule additions, with backups recorded.

## References

- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`
- `src\VideoProcessor-Lib\IRenderer.h`
- `src\VideoProcessor-Lib\microsoft_directshow\MadVRShaderLoader.cpp`
- `src\VideoProcessor-Lib\microsoft_directshow\MadVRExternalPixelShaders.h`
- `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`
- `3rdparty\libplacebo\include\libplacebo\shaders\custom.h`
- `Shaders\NLS.hlsl`
- VP-0034: Restart-free mixed-aspect NLS
- VP-0035: Robust low-latency active-aspect transitions
- VP-0038: Generic viewport state and screen-aware NLS configuration
- VP-0040: Trusted active-picture detection and stable NLS engagement
