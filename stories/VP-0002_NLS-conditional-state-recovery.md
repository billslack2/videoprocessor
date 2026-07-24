# VP-0002: Preserve armed NLS state through conditional aspect fallback

## Scope and repository context

This story applies to the DirectShow/madVR NLS conditional-aspect path in:

`C:\Users\bslac\vp\videoprocessor - VS2026`

It depends on Story 1's request coalescing, but it must be correct even when
used independently. Do not change NLS image geometry in this story; that is
Story 3. This story fixes state ownership, fallback behavior, and restart
control only.

## User story

As a viewer, if I select NLS and VP temporarily sees a non-qualifying active
picture aspect during a transition, NLS should only be temporarily bypassed
when that condition is confirmed. It should automatically resume when the
intended aspect becomes stable again.

## Reproduction and evidence

Use `C:\logs\vp_debug.log` from 2026-07-23.

At `23:28:09`, the active-picture detector published 1.5509, below the NLS
rule's configured `active_aspect_min=1.70`. The timer-driven conditional refresh
then logged:

```text
Shaders: runtime selection changed to "nls_off"
Shaders: armed rule "nls" bypassed at active aspect 1.5509
Conditional shader state changed to 'Nonlinear Stretch (Waiting)'; restarting renderer for aspect negotiation
```

The renderer was torn down and rebuilt, followed by the normal startup graph
re-prime reset at `23:28:15`; this explains madVR's visible HDR-mode flair.
After rebuild, `nls_off` remained selected instead of restoring the original
armed `nls` request. No keyboard input is needed to reproduce this path.

## Root cause in current code

- `VideoProcessorDlg.cpp` calls `IRenderer::RefreshShaderRule` once per second
  while rendering.
- `DirectShowGenericHDRVideoRenderer::RefreshShaderRule` calls
  `MadVRShaderLoader::ApplyConfiguredShaderRule` with `inactive_rule` when the
  active aspect fails validation.
- `MadVRShaderLoader::ApplyConfiguredShaderRule` unconditionally writes its
  argument to global `g_runtimeRuleOverride` under `g_runtimeRuleMutex`.
- Therefore an automatic effective fallback (`nls_off`) overwrites the manual
  runtime request (`nls`) and survives renderer reconstruction.

Relevant files:

- `src/VideoProcessor-GUI/VideoProcessorDlg.cpp` — timer refresh and restart
  scheduling.
- `src/VideoProcessor-Lib/microsoft_directshow/video_renderers/DirectShowGenericHDRVideoRenderer.cpp`
  — `SelectShaderRule` and `RefreshShaderRule`.
- `src/VideoProcessor-Lib/microsoft_directshow/MadVRShaderLoader.cpp`
  — `g_runtimeRuleOverride`, `ApplyConfiguredShaderRule`, and aspect helpers.
- `src/VideoProcessor-Lib/microsoft_directshow/live_source_filter/CBufferedLiveSourceVideoOutputPin.cpp`
  — active-picture aspect detector.

## Implementation plan

1. Model shader state explicitly:
   - **requested rule**: durable user choice, for example `nls`;
   - **effective rule**: chain currently installed, for example `nls` or
     temporary `nls_off`;
   - **fallback reason**: unstable detector, below threshold, source
     transition, or another diagnostic state.
2. Split “set manual runtime request” from “apply an effective rule.” Automatic
   `inactive_rule` application may change the effective chain but must never
   mutate the requested/global manual override.
3. Add aspect hysteresis using stable detector generations or wall-clock time:
   - require sustained stable failure before bypassing;
   - require sustained stable recovery before re-engaging;
   - retain the armed request while the detector reacquires state after a graph
     rebuild.
4. Restart only when the effective output aspect truly changes. Reuse Story 1
   coalescing so one pending restart cannot be duplicated.
5. Automatically reapply the requested rule after qualifying geometry recovers.
6. Log requested rule, effective rule, detector aspect/generation, fallback
   reason, hysteresis state, and aspect transition requiring any restart.

## Verification

1. Select NLS on stable qualifying material.
2. Introduce a short source/menu/aspect transition that produces an invalid or
   below-threshold active-picture measurement, then return to the original
   content.
3. Confirm a transient measurement does not bypass NLS; a sustained failure may
   bypass it, but the requested rule remains `nls`.
4. Confirm NLS automatically returns after stable qualifying geometry recovers.
5. Explicitly select plain-N `nls_off`; confirm it remains a true manual off
   state and is not auto-restored.

## Acceptance criteria

- A transient bad aspect measurement cannot permanently turn NLS off.
- An automatic fallback cannot become the saved/manual rule selection.
- After stable qualifying aspect returns, the original requested NLS rule is
  automatically restored.
- A deliberate manual NLS-off command still disables NLS immediately.
- Restarts are logged as intentional aspect transitions with before/after
  requested and effective rules.
