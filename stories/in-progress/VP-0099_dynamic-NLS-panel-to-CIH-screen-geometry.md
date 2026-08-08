# VP-0099: Dynamic, renderer-neutral NLS geometry and safety policy

## Status

Implementation complete; live renderer review remains (2026-08-08). The implementation branch is
`codex/vp-0099-dynamic-nls-safety`, based on the discovered default integration
branch `v1.1.017-beta` at VP-0098 review head `3c7ebd5`.

The 2026-08-08 developer direction supersedes this story's earlier nested
physical-screen/requested-viewport proposal. VP-0098 established one screen
contract: a configured viewport `screen_aspect` is the presentation target;
when no screen aspect is configured, the actual output panel is the target.
NLS must consume that same target and the final trusted VP-0098 source envelope.

## User story

As a VP user, I want nonlinear stretch to derive its mapping from the current
trusted source envelope and current presentation target, so reasonable mappings
work on any screen aspect while visually destructive mappings fall back to an
ordinary centered fit in both VP Renderer and DirectShow/madVR.

## Required contract

Let `A` be the final source-envelope aspect selected by VP-0098 and `T` be the
resolved presentation target:

- `T = screen_aspect` when the selected viewport explicitly configures it;
- otherwise `T` is the current output-panel aspect for VP Renderer;
- DirectShow receives the same resolved viewport target from the application
  snapshot and must not infer madVR's physical screen, masking, or zoom state;
- `ratio = max(T / A, A / T)`;
- `A < T` selects horizontal warp, `A > T` selects vertical warp, and values
  within `tolerance_percent` are a linear passthrough; and
- a ratio above the rule's validated `max_stretch_ratio` selects safe linear
  fit, never a capped nonlinear warp that silently misses or overshoots the
  target.

The shipped default must allow the representative 4:3 to 16:9 (`1.3333x`) and
16:9 to 2.35:1 (`1.3219x`) mappings while rejecting 4:3 to 2.35:1 (`1.7625x`),
4:3 to 2.76:1 (`2.07x`), and similarly extreme requests. The shader's own
hard bound remains a final defense, not the policy authority.

## Scope

1. Extract or rename the aspect decision as renderer-neutral NLS policy shared
   by VP Renderer and DirectShow/madVR.
2. Add validated per-rule `max_stretch_ratio` configuration with a conservative
   shipped default that supports the required examples. The same value must
   reach both backends and diagnostics.
3. Remove the deprecated `normal`/`scope` screen-profile path and its hard-coded
   16:9/2.35 NLS target selection. Unified viewport profiles and
   `screen_aspect` are the only application-owned target-selection mechanism.
4. Make VP Renderer use the exact current panel aspect when no viewport target
   is configured. No fallback may assume a 16:9 panel.
5. Continue using the final VP-0098 envelope. NLS must not detect bars, shift
   crop rectangles, or become a second crop-authority owner.
6. Keep madVR boundaries truthful: VP derives shader geometry and output-aspect
   intent, but does not configure madVR's independent physical-screen, zoom,
   masking, or native crop settings.
7. Log only changes, with backend, rule, source and target aspects, requested
   ratio, configured limit, axis, decision, source envelope/generation, and
   fallback reason. OSD labels must be target-neutral (`Passthrough`, not
   `Scope passthrough`).

## Required automated tests

1. Pure policy matrix for 4:3, 16:9, 1.85, 2.00, 2.20, 2.35, 2.40, and 2.76
   source/target combinations in both directions.
2. Explicit proof that the shipped limit allows 4:3 to 16:9 and 16:9 to 2.35,
   but rejects 4:3 to 2.35, 4:3 to 2.76, and 16:9 to 2.76.
3. Boundary, tolerance, invalid/unavailable geometry, narrower-only, and custom
   limit tests, including exact-limit acceptance and above-limit safe fit.
4. Configuration tests for omitted/default, minimum, maximum, malformed, below-
   range, and above-range `max_stretch_ratio` values.
5. Backend selection tests proving Alpha and madVR consume the same rule value
   and produce the same mode, ratio, and axis for identical geometry.
6. Viewport tests proving arbitrary configured targets are used verbatim and an
   unconfigured VP Renderer target follows 16:9, DCI, and synthetic panel
   aspects without a hard-coded fallback.
7. Regression tests proving NLS off and safe fit preserve VP-0098 linear
   envelope/layout behavior and generation changes cannot reuse stale geometry.
8. Complete native test suite and clean x64 Release solution build.

## Live validation

Validate both VP Renderer and madVR with NLS on/off across:

- 4:3 to 16:9;
- 16:9 to 2.35:1;
- rejected 4:3 to 2.35:1 and 4:3/16:9 to 2.76:1 cases;
- scope passthrough and nearby-aspect tolerance cases;
- mixed-aspect transitions, subtitles, menus, pans, renderer reset/rebuild, and
  renderer handoff; and
- at least one non-16:9 output panel or synthetic viewport target.

Logs must identify the same source envelope and target decision seen on screen.
Safe-fit cases must retain the entire source without an extreme warp.

## Implementation evidence (2026-08-08)

- Added renderer-neutral `NlsGeometryPolicy`, consumed by VP Renderer and
  DirectShow/madVR, with `ACTIVE`, target-neutral passthrough, and geometry-
  preserving `SAFE_FIT` decisions.
- Added validated typed-NLS `max_stretch_ratio` configuration (`1.0` through
  shader hard bound `1.5`, shipped default `1.4`) and published the same rule
  value to both backends.
- Removed the application `SetScreenProfile`, normal/scope shortcuts,
  accelerator commands, persisted screen-profile choice, 16:9/2.35 selection
  branches, and scope-era viewport aliases. `screen_aspect` is the only
  explicit application target.
- VP Renderer resolves an omitted target from the current output client/
  swapchain aspect; DirectShow reports NLS unavailable until the selected
  viewport explicitly supplies `screen_aspect`, because madVR owns its display
  geometry.
- Change-only NLS diagnostics now identify backend, rule, source/target,
  requested ratio, configured limit, warp axis, mode, generation, and fallback
  reason. OSD passthrough language is target-neutral.
- Added policy matrices and representative safety examples, limit boundaries,
  malformed/range configuration cases, backend publication parity, arbitrary
  panel target resolution, rational output aspects, safe-fit continuity, and
  removed-alias regression coverage.
- Authoritative verification: `VideoProcessor.sln`, x64 Release, completed
  successfully; `x64\\Release\\VideoProcessor-Test.dll` passed 629/629 tests
  with zero failures or skips. TRX:
  `TestResults\\vp0099-authoritative-final.trx` in the implementation worktree.

The story intentionally remains in progress until the live VP Renderer and
madVR matrix above is exercised on real output hardware.

## Acceptance criteria

- No production NLS path selects a target by `scope`/`normal` name or by a
  hard-coded 2.35/16:9 conditional.
- Both renderers use one shared, configurable safety decision and agree for the
  same source envelope, target, tolerance, direction, and maximum ratio.
- Required reasonable examples engage NLS; required extreme examples select
  safe linear fit.
- Runtime source, viewport, panel, and renderer-generation changes recalculate
  the decision without stale geometry, shader recompilation, or unnecessary
  renderer restart.
- Diagnostics make the source, target, ratio, limit, axis, decision, ownership,
  and fallback reason unambiguous without per-frame log churn.
- Native tests and a clean x64 Release build pass before review.

## Dependencies and related work

- VP-0038 owns generic viewport state and aspect-driven NLS inputs.
- VP-0074 owns dynamic Alpha hook updates and shader cold-start recovery.
- VP-0083 owns anamorphic presentation ordering.
- VP-0085 owns frame-correlated madVR look-ahead and graph-thread application.
- VP-0089 may later consume this policy for optional two-axis balanced NLS.
- VP-0098 owns final source-envelope selection and ordinary centered fit.

## Non-goals

- Projector lens-memory, masking motor, or madVR profile control.
- Reintroducing separate physical-screen and requested-viewport ratios.
- Allowing NLS to crop or translate the trusted source envelope.
- Implementing VP-0089's two-axis balanced warp.
