# VP-0058: ReShade compatibility prototype with madVR

## Status

Backlog spike. This is a local compatibility experiment, not a committed
renderer feature or a distribution change.

## User story

As a VideoProcessor user who renders through madVR, I want to know whether
ReShade can safely apply a configurable final post-processing pass to the
madVR output, so I can evaluate its shader ecosystem without destabilizing
capture, timing, HDR treatment, or the existing renderer path.

## Background

ReShade is a generic post-processing injector. Its official site states that
it supports Direct3D 9/10/11/12, OpenGL, and Vulkan, and that effects are
written in ReShade FX. It is installed against an executable and operates by
intercepting the target process's graphics API calls.

When madVR is selected by VP, the relevant question is whether its final
presentation runs in the `VideoProcessor.exe` process and through an API that
ReShade can reliably intercept. If so, ReShade would be a post-renderer effect
chain: it would see madVR's composed output rather than VP's raw capture
frames, conversion buffers, metadata pipeline, queue, or frame timing.

This distinction is important. A successful prototype would not make ReShade
a VP shader implementation, and would not give VP ownership of ReShade's
effect settings, presets, performance, color pipeline, or lifecycle.

## Constraints

- Do not bundle, copy, redistribute, or automatically download ReShade
  binaries, shaders, preset packs, or add-ons. The official ReShade site says
  binaries and shader files must not be shared; users must obtain them from
  ReShade directly.
- Do not inject ReShade programmatically, modify its files, bypass its
  installer, alter its signing, or attempt to conceal it from security tools.
- Do not change VideoProcessor source, installer/release contents, default
  configuration, shader configuration, or help during this spike.
- Do not use the full-add-on build. Its official site describes that build as
  unsigned; it is outside this experiment.
- Do not evaluate anti-cheat compatibility. VP's capture/render use case is
  local video playback only.

## Prototype plan

1. Take a complete backup or restore point of the local test deployment,
   including its ReShade-related files if any. Keep `VideoProcessor.cfg` and
   `VideoProcessorRenderer.cfg` unchanged.
2. Use the normal ReShade installer from `https://reshade.me/` and target a
   dedicated test copy of `VideoProcessor.exe`, not the normal deployment.
   Select only the graphics API that the installer and observed madVR path
   identify; do not guess or install multiple proxy DLLs.
3. Begin with no third-party effects enabled. Confirm that the test process
   launches, madVR initializes, HDR/SDR and fullscreen/windowed presentation
   work, and ReShade's overlay can be opened without a VP crash or renderer
   restart loop.
4. Enable one lightweight, clearly visible final-pass effect (for example a
   simple color adjustment) solely to prove that ReShade sees the final madVR
   image. Record the selected API, DLL name/location, process ID, renderer
   mode, display mode, and effect timing.
5. Test a representative matrix: SDR 23.976, SDR 59.94, HDR10 input tone
   mapped by madVR, fullscreen/windowed presentation, renderer restart, and
   one display-refresh switch. Include an Alpha-renderer control case only to
   prove the experiment is restricted to madVR; do not pursue Alpha support in
   this story.
6. Measure visible correctness, madVR present/render timing, VP conversion
   time, VP and madVR queue health, capture misses, dropped/repeated frames,
   HDR/SDR output behavior, OSD, and stability. Remove ReShade and repeat the
   same cases as the baseline.
7. Remove all ReShade files from the dedicated test copy at the end of the
   spike unless the developer explicitly asks to retain that test environment.

## Questions to answer

- Which graphics API and process boundary does madVR actually use under VP?
- Does ReShade hook the final madVR presentation reliably in windowed,
  exclusive/fullscreen, and refresh-switch paths?
- Does the hook introduce queue starvation, elevated latency, dropped frames,
  mode-switch failures, stale frames, HDR/SDR signaling errors, or crashes?
- Does ReShade affect the final image after madVR tone mapping, and therefore
  require users to treat its effects as display-space adjustments rather than
  source-space processing?
- Can it coexist with VP's existing shader/NLS and renderer lifecycle without
  any VP code changes?
- What clear unsupported conditions should be documented if the prototype is
  viable (for example unsupported API, presentation mode, HDR behavior, or
  renderer choice)?

## Evidence and decision

Record the ReShade version, install/API selection, test executable location,
effect/preset name, renderer mode, source signal, display mode, measured
performance, VP log excerpts, and removal result. Do not commit ReShade
binaries, effects, presets, logs containing user paths, or screenshots with
private content to the repository.

Choose one outcome:

1. **Not compatible:** remove the test integration and document the concrete
   incompatible boundary.
2. **Compatible as an unsupported user experiment:** document only a short
   external setup note pointing users to ReShade's official installer, with
   explicit caveats and no VP code/release changes.
3. **Candidate optional integration:** create a separate design story covering
   support policy, configuration ownership, lifecycle, reset/rebuild behavior,
   color/HDR contract, diagnostics, security/signing implications, and a
   validation matrix. Do not promote this spike directly to implementation.

## Acceptance criteria

- The experiment establishes, from logs and observed presentation behavior,
  whether ReShade can or cannot hook final madVR output under VP.
- The result distinguishes final display-space post-processing from VP's
  source/capture processing.
- VP source, releases, default configuration, and shipped runtime contents are
  unchanged.
- ReShade is obtained and installed only by the tester from its official
  source; no ReShade content is redistributed by VP.
- The conclusion includes a reproducible setup/removal procedure and measured
  performance/stability evidence.

## References

- [ReShade official site](https://reshade.me/): supported graphics APIs,
  ReShade FX, installer workflow, and distribution restriction.
- `src\VideoProcessor-Lib\microsoft_directshow\video_renderers\DirectShowVideoRenderer.*`
- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`: renderer lifecycle and
  handoff coordination.
