# VP-0089: VP Renderer NLS+ balanced stretch

## Status

Done (2026-08-23). Accepted for completion. The implementation from
`codex/vp0089-vp0131-nls-profiles`, including the refined NLS+ shader,
defaults, and configuration-editor changes, is integrated into
`v1.2.001-beta` and deployed. The existing picture-quality observations remain
recorded below as operational evidence, not as a completion gate.

Readiness review confirmed that VP Renderer already owns the exact source
rectangle, viewport target, one-axis mapping decision, dynamic
`stretch_ratio`/`warp_axis` hook parameters, and restart-free profile
selection needed by this work. The new transform is bounded GLSL math plus a
small typed-parameter extension; no API, ownership, or concurrency spike is
needed. The current `NLS.glsl` path remains the compatibility baseline.

Implementation checkpoint (2026-08-15): committed as `847c865` on the
feature branch. Added the separate `NLSPlus.glsl` hook, `axis_balance`
validation/substitution, demo and deployed `NLS+` profiles, diagnostics,
documentation, tests, build copying, and the immutable release-manifest entry.
The existing `NLS.glsl` and `NLS.hlsl` files were not changed.

Verification checkpoint: the clean x64 Release solution build passed; all 41
focused NLS/configuration tests passed; the separate Config application suite
passed; and release packaging staged and verified all 56 immutable files. The
full native suite passed 832 of 837 tests. Its five failures are pre-existing
stale documentation/config test expectations on the default branch (including
the removed `default_screen_profile` key and a `2.1:1` fixture that asserts
`47:20`), not failures in the NLS paths. Live picture-quality acceptance with
actual VP Renderer material remains open as a release-review item.

Runtime QA checkpoint (2026-08-15): logs proved NLS+ selected, parsed, bound,
compiled, and rendered, but the prior equation-only unit test could not prove
that output pixels moved. Added a real libplacebo/D3D11 WARP readback test of
the checked-in GLSL. It proves balanced NLS+ moves both coordinate axes,
ratio `1` is identity on the same dynamic hook, fixed edges remain bounded,
and `axis_balance: 0` matches the existing `NLS.glsl` output within tolerance.
Change-only diagnostics now report the bound dynamic values, expected X/Y
centre scales, hook count, crops, render result, and detector/presentation
geometry. No viewport profile or renderer is created by NLS+.

QA fix checkpoint: committed as `cf2d8fc`. A clean x64 Release rebuild reported
`VERSION_DIRTY=false`, and the final focused run passed all 45 selected
NLS/GLSL/configuration tests. The matched EXE and VP Renderer DLL were deployed
and SHA-256 verified. Live picture-quality acceptance remains open as a
release-review item.

Deployment checkpoint: the clean `847c865` executable/VP Renderer DLL pair,
Config binaries, `NLSPlus.glsl`, documentation, release manifest, demo config,
and minimally edited active config were installed at `C:\Videoprocessor\vp`.
The prior files are recoverable from
`C:\Videoprocessor\vp\backups\vp0089-vp0131-20260815-201830`.

Perceptual-geometry refinement (2026-08-16): commit `de35d43` changes only the
separate NLS+ path. NLS+ now preserves the required centre-aspect quotient at
every strength, treats `strength` as the requested amount of complementary-axis
sharing, caps resulting centre zoom with `max_center_zoom`, and provides
independent horizontal and vertical protected regions. The shipped defaults are
`axis_balance: 0.25`, `max_center_zoom: 1.08`, horizontal protection `0.35`,
vertical protection `0.25`, `aspect_direction: any`, and the shared VP Renderer
presentation crop limit of `2` percent. Existing NLS/NLS-V shader files,
profiles, mapping decisions, direction handling, and defaults are unchanged.
Older bundled NLS+ profiles receive filename-scoped defaults for the three new
shader tokens so updating `NLSPlus.glsl` cannot turn them into compile failures.

Refinement verification and deployment: a clean serial x64 Release rebuild at
`de35d43` reported `VERSION_DIRTY=false`; all 10 focused NLS/NLS+/VP-0131 tests
passed, including production libplacebo/D3D11 pixel readback and old-profile
upgrade coverage; all 44 Config tests passed; and the release packager verified
all 56 immutable files. The matched executable/VP Renderer pair, Config binaries,
`NLSPlus.glsl`, documentation/example config, and minimally edited active config
were SHA-256 verified after deployment to `C:\Videoprocessor\vp`. The active
profile preserved the user's tolerance, direction, maximum ratio, crop
preference, and `Shift+P` shortcut. Recovery material and the complete pre-state
hash manifest are at
`C:\Videoprocessor\vp\backups\nls-plus-de35d43-20260816-091919`.

## User story

As a scope-screen user, I want an opt-in NLS+ profile that shares the required
16:9-to-scope aspect correction across both picture axes, so sports and
broadcast material can avoid concentrating all visible distortion at the
left and right edges.

## Accepted product contract

- `NLS+` is a new VP Renderer-only profile backed by a new
  `shaders/NLSPlus.glsl` hook.
- NLS+ applies a continuous, monotonic inverse mapping on X and Y in the same
  frame. `axis_balance` requests how the aspect correction is shared; the
  shipped profile uses a conservative `0.25` logarithmic share and the
  centre-zoom cap may reduce the effective share for larger corrections.
- The existing `NLS.glsl` and `NLS.hlsl` files and existing NLS profiles keep
  their current behavior, parameters, cache identity, and defaults.
- NLS+ is selected explicitly. Ordinary NLS profiles never receive NLS+
  defaults; an older profile using the exact bundled `NLSPlus.glsl` filename
  receives conservative defaults for newly required shader tokens.
- Like established VP Renderer NLS, NLS+ consumes the trusted active-picture
  rectangle directly; detected source-baked bars are not a separate viewport
  profile or a new renderer.
- NLS+ is GLSL/VP Renderer only. There is no HLSL implementation, madVR
  fallback, compatibility claim, or future DirectShow parity commitment.
- VP-0131 owns the shared, opt-in VP Renderer presentation-crop policy used by
  NLS+, NLS-V, and the existing VP Renderer NLS profiles.

## Configuration contract

NLS+ remains a typed `shader_type: nls` member and reuses `strength`,
`geometry`, `curve`, `quality`, `tolerance_percent`, `max_stretch_ratio`, and
`aspect_direction`.

Add `axis_balance`, a validated number from `0.0` through `1.0`:

- `0.0` is the existing selected-axis transform;
- `0.5` requests an equal split between X and Y in log-aspect space; and
- `1.0` requests correction on the opposite axis; the centre-zoom cap can
  reduce the effective share.

`strength` scales the requested share toward the complementary axis; it does
not weaken the total aspect correction or allow centre geometry to retain the
wrong aspect. Add `max_center_zoom`, validated from `1.0` through `1.25`, to
bound the uniform centre enlargement created by two-axis sharing. Add
`horizontal_center_protection` and `vertical_center_protection`, each validated
from `0.0` through `0.45`, so the axes do not have to share one protected-zone
size. The shipped NLS+ profile uses `strength: 1.0`, `axis_balance: 0.25`,
`max_center_zoom: 1.08`, protections `0.35`/`0.25`, and
`aspect_direction: any`. Geometry-dependent ratio and direction remain dynamic
hook parameters; changing stable content aspect must not create a shader
variant.

## Scope

1. Add `NLSPlus.glsl` without modifying the existing GLSL or HLSL shader.
2. Generalize the established bounded one-dimensional inverse map so it can
   map X and Y with independently derived centre slopes while fixing both
   source edges and sampling only within `[0,1]`.
3. Derive the two axis slopes from the runtime aspect ratio and direction so
   their quotient exactly represents the required source-to-target correction
   at every strength.
4. Parse, validate, substitute, document, and test `axis_balance`,
   `max_center_zoom`, and the separate axis-protection fields without adding
   dedicated Config controls. The existing generic parameter table is the UI.
5. Add the NLS+ member to the demo and deployed configuration while leaving
   the selected NLS state unchanged.
6. Report the selected file/profile, direction, ratio, balance, resolved
   presentation crop, and mapping reason in change-only diagnostics.

## Acceptance criteria

- Existing NLS rule resolution, hook keys, source files, decisions, and golden
  mapping cases are unchanged when new settings are absent.
- Requested balance across the validated range produces bounded, finite,
  monotonic X/Y maps, preserves the centre aspect by construction at every
  strength, and never exceeds configured centre zoom.
- Balance `0.0` reduces to the current selected-axis mapping at full strength.
- The hook updates aspect/direction without recompilation, renderer restart,
  cache churn, or stale-frame presentation.
- Invalid balance values reject the NLS+ rule safely.
- NLS+ has no HLSL file and cannot be selected as a madVR effect.
- Unit/policy tests, shader-source contract tests, a clean x64 Release build,
  real GPU pixel-readback tests, and live VP Renderer validation cover geometry,
  pans, tickers, faces, menus, and mixed-aspect transitions.

## Non-goals

- Copying, decompiling, deriving from, or claiming compatibility with any
  proprietary NLS+ algorithm.
- madVR/HLSL NLS+, intentional presentation crop, or target-fill behavior,
  now or as deferred parity work.
- Neural, semantic, face-aware, object-aware, or motion-aware warping.
- Dedicated Config controls or a Config layout change.
- Changing capture format, subtitle placement, or active-picture detection.

## References

- VP-0131, VP-0034, VP-0035, VP-0080, VP-0083, VP-0099
- `shaders/NLS.glsl`
- `src\VideoProcessor-Lib\NlsGeometryPolicy.*`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`
