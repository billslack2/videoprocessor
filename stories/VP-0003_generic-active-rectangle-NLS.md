# VP-0003: Make existing NLS active-rectangle-aware for 4:3 and all content

## Scope and repository context

This story extends the existing NLS behavior for the DirectShow/madVR renderer.
It deliberately does **not** add a separate 4:3 rule, shortcut, or user-facing
configuration profile. Once the existing NLS rule is selected, geometry must
come from the detected active image rectangle and the actual render viewport.

Source repository:

`C:\Users\bslac\vp\videoprocessor - VS2026`

Current deployed NLS configuration and shader:

- `C:\Videoprocessor\vp\VideoProcessor.cfg`, `[shaders.nls]`
- `C:\Videoprocessor\vp\Shaders\NLS.hlsl`

The current NLS rule targets 2.35:1 for Scope use. This story must preserve
that behavior when that is the actual target viewport while also allowing
generic NLS to use a normal 16:9 viewport without a separate rule.

## User story

As a viewer, once I enable NLS, VP should automatically use the real active
picture rectangle and actual render viewport to select the correct nonlinear
geometry. This includes filling a 16:9 screen from 4:3 pillarboxed content
without a separate 4:3 configuration choice.

## Current limitation

`[shaders.nls]` currently has `active_aspect_min=1.70`, so it rejects 4:3
content. `Shaders\NLS.hlsl` receives only `active_height_fraction`; it can crop
top/bottom letterbox bars but cannot crop left/right pillarbox bars. The
existing active-picture detector calculates left/right/top/bottom locally but
only publishes a stable aspect ratio to renderer consumers.

For 4:3 into a 16:9 viewport, required horizontal expansion is:

```text
(16 / 9) / (4 / 3) = 1.333333...
```

This is within the shader's existing 1.5x safe stretch limit. Correct output
still requires sampling from the detected 4:3 active rectangle rather than
stretching its encoded side bars.

## Relevant implementation points

- `src/VideoProcessor-Lib/microsoft_directshow/live_source_filter/CBufferedLiveSourceVideoOutputPin.cpp`
  - `UpdateActivePictureAspectRatio` detects black boundaries by sampling frame
    edges. Extend this stable result to include rectangle bounds.
- `src/VideoProcessor-Lib/microsoft_directshow/live_source_filter/CBufferedLiveSourceVideoOutputPin.h`
  - currently publishes `m_activePictureAspectRatio` and
    `m_activePictureAspectStable`; add a thread-safe active-rectangle snapshot
    or equivalent getter.
- `src/VideoProcessor-Lib/microsoft_directshow/video_renderers/DirectShowVideoRenderer.*`
  - exposes active-picture data from the live source to the generic HDR
    renderer.
- `src/VideoProcessor-Lib/microsoft_directshow/video_renderers/DirectShowGenericHDRVideoRenderer.cpp`
  - supplies runtime geometry to `MadVRShaderLoader`.
- `src/VideoProcessor-Lib/microsoft_directshow/MadVRShaderLoader.cpp`
  - currently derives `active_height_fraction`, `stretch_ratio`, and
    `warp_axis` before substituting shader parameters.
- `Shaders/NLS.hlsl`
  - currently maps vertical active height only; it must map the complete active
    source rectangle before nonlinear sampling.

## Implementation plan

1. Publish a stable active-picture rectangle: left, top, right, bottom,
   raster width/height, stable flag, and generation/version. Readers must take
   a coherent snapshot and never combine bounds from different frames.
2. Thread that rectangle through the renderer to `MadVRShaderLoader` alongside
   the existing active-picture aspect. Preserve current behavior if no stable
   rectangle is available.
3. Add generic shader parameters for normalized active left/right/top/bottom,
   source active aspect, target viewport aspect, selected axis, and safe
   stretch ratio.
4. Update `NLS.hlsl` to map texture coordinates through the complete active
   rectangle first. This removes pillarbox and letterbox bars from the source
   sampling region before applying nonlinear horizontal or vertical mapping.
5. Derive target geometry from the actual render viewport/output aspect, not a
   new 4:3 rule. For a normal 16:9 viewport and 4:3 source, crop the side bars
   and use 1.333x horizontal nonlinear expansion. For Scope or another active
   viewport, use that viewport's actual aspect.
6. Preserve a protected center and concentrate distortion near edges. Continue
   to enforce monotonic mapping and reject ratios above the proven safe range
   rather than silently clipping or excessively distorting content.
7. Decide and document generic NLS aspect eligibility: it must allow 4:3 after
   stable bounds exist, while avoiding activation on unstable/menu/transition
   measurements. Coordinate this with Story 2's hysteresis rather than relying
   solely on the present `active_aspect_min=1.70` gate.
8. Add OSD/debug diagnostics for active rectangle, source aspect, target
   viewport aspect, crop fractions, warp axis, calculated ratio, and any
   safety-limit refusal.

## Verification

Use static test material whose active rectangles are known:

1. 4:3 pillarboxed in a 16:9 raster: verify side bars are removed, source
   fills 16:9, and derived ratio is about 1.333x.
2. 16:9 full-frame content: verify no active-rectangle crop or nonlinear
   stretch is introduced unless the selected NLS target genuinely differs.
3. Letterboxed widescreen content: verify top/bottom crop and existing Scope
   behavior remain correct.
4. Aspect/menu/source transitions: verify no stale rectangle or one-frame
   boundary detection produces visible jumps; Story 2 hysteresis should hold
   current safe behavior until stable geometry is available.
5. Evaluate faces, circles, logos, subtitles, and moving side graphics for
   center protection, edge distortion, aliasing, and bar leakage.

## Acceptance criteria

- Existing NLS on stable 4:3 pillarboxed content fills a 16:9 viewport without
  a separate rule, shortcut, or configuration profile.
- The 4:3 source uses approximately 1.333x horizontal nonlinear expansion,
  with materially less distortion in the protected center than near the edges.
- VP removes detected side bars from NLS sampling; it does not stretch black
  bars into the picture.
- Existing letterboxed widescreen/Scope NLS behavior continues through the same
  generic active-rectangle mechanism.
- Unstable or unavailable active-picture data leaves the current image intact
  until a stable geometry decision is available.
