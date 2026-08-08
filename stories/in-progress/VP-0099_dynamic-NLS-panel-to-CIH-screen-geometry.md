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
  `TestResults\\vp0099-authoritative-final2.trx` in the implementation worktree.
- Review candidate source commit: `e3ffc32` (`VP-0099 make NLS geometry
  renderer neutral`) on the local implementation branch.

The story intentionally remains in progress until the live VP Renderer and
madVR matrix above is exercised on real output hardware.

## Deployed test setup (2026-08-08)

- Deployed source commit `e3ffc32` from its successful x64 Release build to
  `C:\\Videoprocessor\\vp`; deployed executable and VP Renderer DLL hashes match
  the verified build artifacts.
- Preserved the active configuration and added only explicit
  `max_stretch_ratio: 1.4` settings to both NLS variants plus an isolated F7
  `2.76:1` safety-test viewport. Existing F3 `16:9`, F2 `2.35:1`, and F8
  `2.53:1` targets and all other user settings remain intact.
- Backups use suffix
  `.pre-vp0099-e3ffc32.20260808-091424.bak` for the executable, VP Renderer
  DLL, active configuration, and configuration reference.
- Startup smoke passed: the deployed app remained responsive, loaded VP
  Renderer plugin API 8, parsed the unified configuration, and applied the
  explicit DirectShow base target `16:9` without configuration errors. The app
  was left running for live testing (startup PID 32016).

### Live finding: madVR same-axis crop limitation

The first live madVR test exposed a remaining parity gap. At 09:36:21 VP
detected the trusted active picture as `2.0000` (`0,120-3840,2040`) against the
F3 `16:9` target. The requested ratio was only `1.125`, below the configured
`1.4` limit, but madVR selected `safe_fit` with reason `geometry cannot be
mapped before madVR crop`. No shader was installed, matching the observed lack
of a visual change.

This was not a configuration/load failure: after selecting the F2 `2.35:1`
target, the same `2.0000` source produced an active horizontal mapping at
`1.175`; madVR accepted dynamic picture aspect `94:45` on the next sample for
both standard and protected NLS. However, the same-axis restriction could also
block the required 4:3 pillarbox-to-16:9 horizontal mapping. VP-0099 must remain
open until DirectShow/madVR can safely perform that required case or the
renderer-specific limitation is explicitly redesigned and accepted.

### Same-axis madVR research and proposed correction (2026-08-08)

The live failure is caused by the current HLSL sampling contract, not by the
renderer-neutral safety policy or a missing madVR capability. Before commit
`4cd3e44`, the madVR shader mapped the entire output texture through the trusted
active rectangle. That removed encoded bars in VP while madVR independently
applied its own `videoCropRect`, producing a double crop. The conservative fix
made madVR the sole bar-removal owner by forcing the shader sample rectangle to
the full raster, but a same-axis bar is then sampled through the nonlinear map.
The current guard therefore rejects horizontal NLS with pillarbox bars and
vertical NLS with letterbox bars.

Primary-source comparison against MPC-BE `d2c7b28` and MPC Video Renderer
`fd3a829` confirms the available rendering contracts:

- MPC-BE maps frame-space and screen-space shader targets directly to madVR's
  published pre-scale and post-scale shader stages.
- MPC Video Renderer's release path accepts post-scale shaders; its pre-scale
  shader method is debug-only and returns `E_NOTIMPL` in release builds.
- MPC Video Renderer also returns `E_NOTIMPL` for
  `IBasicVideo::SetSourcePosition`, so an upstream source-rectangle workaround
  is not a portable DirectShow contract.
- madVR's published API exposes both shader stages, the independently detected
  `videoCropRect`, and transient zoom/aspect commands including
  `setArOverride` (documented as applying before cropping). MPC therefore
  confirms that shader-based stretch is supported, but it does not contain a
  built-in NLS implementation that already solves this exact double-crop case.

The preferred correction is a bar-preserving, active-local pre-scale shader:

1. Keep the trusted active rectangle and the existing compensated whole-raster
   aspect calculation
   `raster DAR = target DAR * active height / active width`.
2. Pass the measured active rectangle to the HLSL shader instead of replacing
   it with full-raster bounds.
3. For pixels outside that rectangle, return the original sample unchanged so
   all encoded bars and their boundaries remain intact.
4. For pixels inside it, normalize the selected axis within the active
   rectangle, apply the existing monotonic NLS map, convert the mapped
   coordinate back into the active rectangle, and clamp reconstruction taps to
   that rectangle to prevent bar bleed.
5. Let madVR apply its independently detected crop exactly once. Because the
   active boundaries are unchanged and the shader maps both active edges back
   to themselves, the compensated raster DAR yields the configured target DAR
   after madVR's crop.
6. Remove the same-axis full-width/full-height rejection. Retain stable-
   generation, finite-bounds, target, ratio, and configured safety checks.

This design preserves the current pre-resize quality path and does not require
changing madVR profiles, disabling black-bar detection, using persistent
madVR settings, or taking ownership of projector/screen geometry. Its steady-
state cost is effectively unchanged: active pixels retain the configured tap
count, bar pixels can exit after one sample, and only inexpensive coordinate
normalization plus an active-boundary branch is added. The existing first-use
shader compilation hitch is separate and remains observable in telemetry.

A post-scale NLS shader is a viable diagnostic fallback because both madVR and
MPC expose that stage, and it naturally operates after the renderer's resize.
It is not the preferred production fix: it can perform the multi-tap warp at
full output resolution, has renderer-specific screen-space semantics, and may
reduce reconstruction quality compared with pre-scale mapping. Disabling
madVR cropping or driving `setArOverride`/zoom state is also less desirable
because it would make VP responsible for renderer-owned user/profile state.

Required correction tests:

- presentation-plan tests must accept horizontal NLS with pillarbox geometry,
  vertical NLS with letterbox geometry, and windowbox geometry while retaining
  the compensated-DAR identity;
- shader-coordinate tests must prove exterior pixels are unchanged, active
  edges map exactly to themselves, centre/edge monotonicity is preserved, and
  all quality taps remain inside the active rectangle;
- stale, unstable, malformed, and out-of-range geometry must still fail safe;
- HLSL preflight must succeed for every quality and geometry variant; and
- live madVR validation must cover native full-raster, encoded pillarbox,
  letterbox, and windowbox samples for both warp axes, with logs comparing VP's
  trusted rectangle to madVR `videoCropRect`, `videoOutputRect`, and
  `croppedVideoOutputRect`. The required 4:3-to-16:9 case must report `active`
  and visibly engage NLS, while 4:3-to-2.35/2.76 remains `safe_fit`.

### Same-axis madVR correction implemented and deployed (2026-08-08)

- Source commit `e31e7ba` implements the preferred active-local pre-scale
  design. The shader now returns the original sample outside the trusted
  active rectangle, maps only the selected coordinate inside that rectangle,
  and clamps every reconstruction tap to its active boundary. The presentation
  plan retains the measured rectangle and no longer rejects bars on the warp
  axis; malformed or stale geometry still resolves to native safe fit.
- Added DirectShow/madVR coverage for horizontal pillarbox, vertical
  letterbox, windowbox, and the required 4:3 pillarbox-to-16:9 compensated-DAR
  case. A source-level HLSL test expands and compiles all 16 runtime combinations
  of quality, geometry, and warp axis as `ps_3_0`.
- The exact committed revision completed an x64 Release build and the full
  `VideoProcessor-Test.dll` suite passed 631/631 twice. Post-commit TRX:
  `TestResults\\vp0099-madvr-same-axis-postcommit.trx` in the implementation
  worktree.
- Deployed the verified executable, paired VP Renderer DLL, and `NLS.hlsl` to
  `C:\\Videoprocessor\\vp`; SHA-256 hashes match the Release artifacts. Prior
  files are backed up with suffix
  `.pre-vp0099-e31e7ba.20260808-100742.bak`. The active configuration was not
  rewritten and retains the `1.4` cap and F2/F3/F7/F8 test targets.
- Startup smoke passed: the app remained responsive, discovered madVR, loaded
  VP Renderer plugin API 8, restored the 16:9 viewport, and started the madVR
  renderer without configuration or shader errors. It was left running as PID
  19832 with NLS off, ready for the live `Shift+n` visual test.

The story remains in progress until live same-axis output confirms the new log
contract (`sample_rect` equals the measured active rectangle), correct visible
stretch, and no double crop on madVR hardware. The VP Renderer regression path
is covered by the same Release suite but should remain in the live matrix.

### madVR transition churn and NLS-exit restart correction (2026-08-08)

Live switching exposed two additional DirectShow issues. The unified off
selector is `@shader-key:n`, but dynamic native-DAR restoration was still
guarded by the removed legacy name `nls_off`; exit therefore cleared NLS and
requested a renderer replacement while entry used accepted dynamic media-type
renegotiation. Repeated standard/protected switches also showed synchronous
madVR shader installation taking approximately 18-69 ms while VP cleared both
stages and then cleared the target stage again.

Commit `3c3df3b` corrects both paths:

- every effective output-aspect change now attempts `QueryAccept`-guarded
  dynamic renegotiation, including restoration to native DAR, before changing
  the active shader chain; renderer replacement remains the fallback only when
  downstream rejects the candidate media type;
- shader files are expanded and locally compiled while the prior coherent
  chain remains active;
- a fingerprinted per-renderer chain state mutates only changed stages, clears
  each changed stage once, leaves unrelated stages untouched, and coalesces an
  identical expanded chain without any madVR clear/add calls;
- an installation failure attempts to restore the previous prepared stage; and
- transition telemetry separates preparation, clear, madVR install, and total
  time and reports changed/unchanged stages, coalescing, and rollback result.

The exact commit completed an x64 Release build and passed 633/633 tests twice.
Post-commit TRX:
`TestResults\\vp0099-madvr-transition-postcommit.trx`. The paired executable
and VP Renderer DLL were deployed to `C:\\Videoprocessor\\vp` with matching
Release hashes; backups use suffix
`.pre-vp0099-3c3df3b.20260808-102816.bak`. Configuration and shader files were
unchanged. Startup telemetry proved an empty identical chain was coalesced with
`clear=0.000ms` and `install=0.000ms`. The app was left running as PID 20252
for the user-operated NLS standard/protected/off visual sequence; live
confirmation of the reverse dynamic DAR acceptance and perceived stutter
improvement remains required.

### Coherent madVR transition delivery (2026-08-08)

Live logs from `3c3df3b` proved that madVR accepted both forward and reverse
dynamic picture-aspect changes without an NLS-requested renderer restart.
They also isolated the remaining visible jump: on a cold standard/protected
transition, local HLSL preparation took approximately 30-44 ms and madVR's
non-atomic clear/add operation took approximately 41 ms while samples were
still being delivered. Aspect negotiation could likewise become visible on a
sample before the replacement chain was installed. Logs also showed rapid
modified/unmodified `N` events, which can occur when a held shortcut repeats
after Shift is released.

Commit `932d51b` holds the buffered DirectShow delivery gate across the entire
aspect-and-shader transaction. The last coherent frame therefore remains on
screen until the new DAR and complete shader chain are ready; the first sample
after release carries the pending media-type update. The implementation also
adds physical-key-aware repeat suppression for configured shader shortcuts,
while preserving a deliberate new unmodified key press after key-up. New
telemetry reports gate wait/hold/total timing and identifies coherent
transactions with `delivery_held=1`.

The exact committed revision completed an x64 Release build and passed 634/634
tests. Post-commit TRX:
`TestResults\\vp0099-coherent-transition-postcommit.trx`. The paired executable
and VP Renderer DLL were deployed with matching Release hashes; backups use
suffix `.pre-vp0099-932d51b.20260808-105410.bak`. Configuration and shader
files were unchanged. Startup smoke remained responsive on PID 34160. Live
telemetry confirmed `delivery_held=1`, an accepted dynamic DAR only after the
coherent transaction, and `renderer_restart=0`. A separate one-time
output-readiness graph reset occurred during startup and should not be confused
with an NLS transition restart; final visual validation should be performed
after startup settles.

### Settled last-intent shortcut switching (2026-08-08)

The coherent delivery build was visibly better in the user's live madVR test,
but dropping keyboard repeat messages was not the desired interaction model.
Commit `6c08624` replaces that behavior with a 200 ms, physical-key-aware,
last-intent-wins debounce for configured shader shortcuts. Each genuine
`n`/`Shift+n`/`Shift+p` selection updates the pending mode and restarts the
settle interval; only the final mode is applied after the physical key is up.
An unmodified repeat emitted after Shift release retains the original modified
intent instead of becoming an accidental off selection. Pending intent is
also retained while the renderer is temporarily unavailable or restarting.

The same commit routes deferred NLS activation and withdrawal caused by
active-picture reacquisition through the coherent delivery transaction. This
closes the startup edge observed at 10:55:39, where a pending DAR had otherwise
become visible before the asynchronously refreshed shader chain.

The exact committed x64 Release build is clean and passed 635/635 tests.
Post-commit TRX: `TestResults\vp0099-debounce-postcommit.trx`. The executable
and paired VP Renderer DLL were deployed with matching hashes; backups use
suffix `.pre-vp0099-6c08624.20260808-110151.bak`. Configuration and shader
files were unchanged. One initial launch faulted inside `ucrtbase.dll` during
DirectShow filter enumeration and left a local crash dump; two subsequent
launches completed enumeration and loaded VP Renderer API 8. The final test
instance is responsive on PID 26356 with madVR running.

### Expansion-only NLS safety correction (2026-08-08)

Live madVR testing exposed a real reverse-direction failure while the base
16:9 viewport was active. For a trusted 2.00:1 picture, the omitted direction
policy silently defaulted to `ANY`; VP selected a vertical 1.125 NLS warp and
negotiated DAR `128:81`. madVR consequently reported an adjusted raster of
3840x2430 and reduced the displayed width, so the result visibly shrank even
though the shader described the operation as a stretch. After selecting the
2.35 scope viewport, the same picture correctly used horizontal NLS and DAR
`94:45`.

Commit `e524332` makes typed NLS expansion-only by default in both the current
`[shader.*]` loader and the legacy loader. A known picture wider than the target
now resolves to linear passthrough with native geometry; reverse-direction
vertical warping remains possible only through an explicit
`aspect_direction: any`. The shipped standard and protected profiles state
`aspect_direction: narrower_only` explicitly. New tests cover the exact
2.00-to-16:9 no-shrink case and prove that omitted, explicit expansion-only,
and explicit opt-in behavior resolve identically for madVR and VP Renderer.

The exact committed x64 Release build passed 637/637 tests. Post-commit TRX:
`TestResults\vp0099-expansion-only-postcommit.trx`. The executable and paired
VP Renderer DLL were deployed with matching hashes; binary backups use suffix
`.pre-vp0099-e524332.20260808-111023.bak`. The active configuration received
only the two explicit direction lines after backup to
`VideoProcessor.cfg.pre-vp0099-e524332.20260808-111006.bak`. The app is
responsive on PID 34152. Live telemetry now confirms the exact safety split:
base 16:9 reports `mapping=passthrough`, `axis=none`, native DAR, and
`preserving source geometry`; scope 2.35 reports `mapping=active`,
`axis=horizontal`, and accepted DAR `94:45`; exit restores native 3840:2160.
All three paths report `renderer_restart=0`. User visual confirmation remains
pending.

### Alpha fresh-start transition correction (2026-08-08)

Live Alpha testing showed a one-time bounce that was not shader compilation:
the persistent cache was warm and all recorded NLS compile stages took
`0.000 ms`. The interruption came from VP-0078's unconditional delayed
queue-only re-prime after DirectShow-to-Alpha handoff and again after Alpha
fullscreen-host reconstruction. The current Alpha queue policy already treats
the observed two-to-three-frame reserve as healthy, and the retired DirectShow
queue cannot cross into the newly constructed Alpha renderer.

Commit `bb1932f` records backend and host fresh-start causes for diagnostics but
does not schedule a second delayed queue generation for either cause. The new
Alpha renderer reveals on its first verified live frame. A genuine cross-family
refresh transition retains its existing one-shot delayed re-prime, as do the
separate display-settle and already-covered transition-rebind safety paths.
Telemetry now reports the skipped transition cause and
`action=reveal-on-first-live-frame`; refresh diagnostics report any coalesced
host or backend-handoff marker.

The exact committed x64 Release build is clean (`VERSION_DIRTY=false`) and the
full native suite passed 638/638. Post-commit TRX:
`TestResults\vp0099-alpha-start-postcommit.trx`. The executable and paired VP
Renderer DLL were deployed with matching hashes; backups are
`VideoProcessor.exe.pre-vp0099-bb1932f.20260808-113531.bak` and
`vprenderer\VideoProcessorVPRenderer.dll.pre-vp0099-bb1932f.20260808-113531.bak`.
Configuration was unchanged.

Post-deployment live confirmation is blocked before renderer selection by the
pre-existing intermittent DirectShow filter-enumeration fast-fail. Three
consecutive launches failed in `ucrtbase.dll` with exception `0xc0000409`,
subtype 7, at the same enumeration point observed before this change. Dumps
`VideoProcessor.exe.17404.dmp`, `VideoProcessor.exe.29488.dmp`, and
`VideoProcessor.exe.13932.dmp` are preserved under
`C:\Users\bslac\AppData\Local\CrashDumps`. This failure occurs before the
modified Alpha start policy executes, so automated validation is complete but
the no-bounce live result must not yet be claimed.

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
