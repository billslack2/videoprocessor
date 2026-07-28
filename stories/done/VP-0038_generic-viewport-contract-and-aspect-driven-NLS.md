# VP-0038: Generic viewport state and screen-aware NLS configuration

## Status

Done as of 2026-07-28. Software implementation, automated verification,
documentation, live-configuration migration, deployment, and hardware
validation are complete. The joined implementation merged through PR `#17`
to `v1.1.014-beta` as `71bfb09`.

The user validated the corrected F2/F3 16:9 and 2.35 viewport coordination,
4:3 preservation on the Scope viewport, eligible 4:3-to-16:9 stretching,
mixed Scope/IMAX NLS geometry, OSD, and renderer replacement. Runtime logs
showed `aspect=47:20`, dynamic NLS aspect acceptance, and
`renderer_restart=0`. The exact merge commit passed a clean Release x64 build
and all 181 native tests.

## Implementation progress

- 2026-07-27: Created `codex/vp-0038-generic-viewport` from
  `v1.1.014-beta`.
- 2026-07-27: Commit `1702ad3` introduced the shared exact aspect parser,
  generic viewport keys with deprecated-alias validation, corrected F2/F3
  unified examples, and shared parsing in the libplacebo and DirectShow shader
  paths.
- Verification at this checkpoint: x64 Debug solution build succeeded and all
  155 native unit tests passed.
- 2026-07-27: Built x64 Release from clean commit `1702ad3` and deployed the
  executable plus libplacebo runtime to `C:\Videoprocessor\vp` for hardware
  testing. Existing `VideoProcessor.cfg` and `VideoProcessor.state` hashes
  were verified unchanged. The replaced binaries are recoverable from
  `C:\Videoprocessor\vp\backup-before-vp0038-20260727-235804`.
- 2026-07-28: Commit `eb5e209` completed the application-owned immutable
  profile/runtime snapshot, typed source and viewport variables, application
  key selection and persistence, apply-only renderer boundary, DirectShow and
  libplacebo integration, typed NLS configuration, arbitrary viewport output
  aspects, 4:3-to-2.35 safe fit, migration diagnostics, updated HTML guide,
  cleaned examples, and runtime tests.
- 2026-07-28: Commit `cda7f25` added renderer-neutral active viewport status
  to the runtime OSD.
- Verification: clean x64 Release solution rebuild succeeded with embedded
  version `cda7f25`; all 159 native tests passed.
- Deployment: copied the verified executable, PDB, libplacebo plugin, NLS
  shader, and HTML guide to `C:\Videoprocessor\vp`. SHA-256 hashes matched the
  build outputs. The pre-deployment files are recoverable from
  `C:\Videoprocessor\vp\backup-before-vp0038-final-20260728-015550`.
- Live configuration: backed up
  `VideoProcessor.cfg.before-vp0038-final-20260728-015011.bak`, converted the
  two NLS rules to `type: nls` with typed tuning keys, removed fixed
  target/output/stretch ownership and the redundant `movies_24` no-op rule,
  and preserved the installation-specific `broadcast_sdr` rule and existing
  viewport/profile values.
- 2026-07-28: Commit `b3eb2a8` simplified each shader section to one
  independently parameterized `file` and `stage`. A shortcut now implies
  manual selection; effects sharing a shortcut compose in configured order
  while retaining per-effect parameters. Indexed/flat shader chains are
  rejected rather than retained as migration syntax.
- Shader filenames are restricted to one filename under `Shaders` beside
  `VideoProcessor.exe`; absolute paths, directory components, traversal,
  alternate streams, and configuration-relative shader directories are
  rejected. The runtime OSD joins grouped effect labels and identifies each
  installed effect/file.
- Final verification: clean x64 Release build succeeded with embedded version
  `b3eb2a8`; all 161 native tests passed.
- Final deployment: updated the executable, PDB, HTML guide, and minimally
  migrated live configuration in `C:\Videoprocessor\vp`. Build/deployment
  SHA-256 hashes match. Replaced deploy files are recoverable from
  `backup-before-vp0038-shader-schema-20260728-090003`, and the pre-migration
  live configuration is
  `VideoProcessor.cfg.before-vp0038-shader-schema-20260728-085613.bak`.

## User story

As a viewer who alternates between a physical 16:9 presentation with side
curtains closed and a CIH widescreen presentation with the curtains open, I
want VP's shader configuration to know which viewport I selected, so source
cropping, nonlinear stretch, safe-fit/pillarboxing, and subtitle fitting use
the actual active screen without VP attempting to configure madVR itself.

## Problem

The unified configuration currently models the viewport with Scope-specific
keys:

```ini
[profiles.viewport.normal]
when: $key=="F3" || $key=="Ctrl+F10"
mode: normal

[profiles.viewport.scope]
when: $key=="F2" || $key=="Ctrl+F9"
mode: scope
scope_screen_aspect: 2.35:1
scope_subtitle_fit: true
scope_subtitle_hold_seconds: 2
scope_subtitle_padding_pixels: 30
```

These names encode one particular screen shape even though aspect and subtitle
behavior belong to any viewport. The `normal`/`scope` mode distinction also
duplicates information already expressed by the resolved screen aspect and
profile settings.

Unified key selection is presently renderer-owned through
`IRenderer::SelectUnifiedProfileKey`. Libplacebo implements that flow, but the
DirectShow/madVR renderer does not. DirectShow NLS instead receives the older
boolean Normal/Scope command and otherwise falls back to the NLS rule's static
`nls_target_aspect_ratio`. Consequently `[profiles.viewport.*]` is not yet a
renderer-neutral source of shader-selection state.

The physical-display action remains external: the same F2/F3 press may switch
madVR profiles, projector lens memory, or curtain automation through their own
configuration. VP records its own viewport selection from that key event; it
does not send the viewport aspect or subtitle settings to madVR and does not
claim that the external action succeeded.

## Proposed configuration

Viewport profiles use generic settings:

```ini
[profiles.viewport.normal]
when: $key=="F3" || $key=="Ctrl+F10"

[profiles.viewport.scope]
when: $key=="F2" || $key=="Ctrl+F9"
screen_aspect: 2.35:1
subtitle_fit: true
subtitle_hold_seconds: 2
subtitle_padding_pixels: 30
```

Defaults:

- No selected/configured viewport means `screen_aspect: 16:9`.
- A viewport profile with no `screen_aspect` also resolves to `16:9`.
- `subtitle_fit` defaults to `false`.
- Subtitle hold and padding retain the existing safe defaults and are inert
  when subtitle fitting is disabled.
- Profile names such as `normal` and `scope` are labels only; they do not
  determine geometry.
- `mode: normal|scope` is not required for viewport geometry. Any remaining
  mode-specific renderer behavior must be expressed through resolved generic
  settings rather than inferred from a profile name.

The corrected default bindings remain F2 for the 2.35 viewport and F3 for the
default 16:9 viewport. Alternate bindings such as Ctrl+F9/Ctrl+F10 resolve the
same profiles through their `when` expressions.

## Aspect syntax

Use one shared parser and normalized aspect representation everywhere viewport
or output aspect is accepted.

Required accepted forms include:

- `4:3`
- `16:9`
- `16x9` and `16X9`
- `2:1`
- `2.2:1`
- `2.35:1`
- decimal shorthand such as `1.7777778` or `2.35`

Requirements:

1. Allow surrounding whitespace and whitespace around `:`, `x`, or `X`.
2. Accept positive integer or decimal components.
3. Reject zero, negative, missing, repeated-delimiter, trailing-junk,
   non-finite, and out-of-range values with a section/key-specific error.
4. Support at least the inclusive resolved range `1.0..4.0`, which includes
   4:3 and the existing widescreen range.
5. Preserve a useful rational form for exact comparison, diagnostics, and
   downstream geometry calculations when possible:
   `16:9` remains 16:9, `2.2:1` normalizes to 11:5, and `2.35:1` normalizes to
   47:20 or an equivalent exact ratio.
6. Do not maintain separate, subtly different aspect parsers in the profile,
   DirectShow, madVR shader, and libplacebo paths.

## Required runtime contract

Introduce an application-owned resolved viewport value containing at least:

- selected group/profile identity;
- normalized screen aspect as both a numeric ratio and canonical
  numerator/denominator;
- subtitle-fit enabled state;
- subtitle hold duration;
- subtitle padding;
- selection generation/version.

Required behavior:

1. Resolve `[profiles.viewport.*]` outside an individual renderer backend.
   One physical key event selects the viewport once and publishes one coherent
   value to VP's profile/shader runtime.
2. Replace the boolean `SetScreenProfile(scopeScreen, ...)` boundary with an
   aspect/settings-driven viewport API, or adapt it behind a new generic API
   until all callers migrate.
3. Publish the viewport to shader configuration through stable read-only
   variables. At minimum provide numeric `$screen_aspect` and text
   `$viewport_profile`; expose subtitle values through the same resolved
   snapshot where shader/renderer configuration needs them.
4. Shader rules and parameter expressions read those variables from one
   coherent snapshot. Floating-point equality must not be required to
   distinguish common screens; ratio-aware comparison or documented tolerance
   must be used.
5. Renderer switches restore VP's selected viewport variables without
   requiring another F2/F3 press.
6. DirectShow NLS chooses its rule/mapping from detected content geometry plus
   `$screen_aspect`, not from a separate hard-coded Scope assumption.
7. NLS derives active-rectangle crop, stretch ratio, warp axis, passthrough,
   and safe-fit/pillarbox behavior from that content/screen combination.
8. VP does not pass `screen_aspect`, viewport profile identity, or subtitle
   profile values to madVR as configuration. madVR remains independently
   configured and may react to the same physical hotkey.
9. A 4:3 active picture targeting the default 16:9 viewport retains the
   existing approximately 1.333x NLS behavior. With NLS deliberately off,
   madVR retains normal pillarboxing.
10. A 4:3 active picture on a 2.35 viewport must not be nonlinearly stretched
    to 2.35. Preserve its 4:3 geometry with a restart-free safe-fit/pillarbox
    mapping while NLS remains armed.
11. A 16:9 or 1.90 active picture targeting a 2.35 viewport uses horizontal
   NLS; matching 2.35 content uses linear passthrough.
12. Content-aspect changes under one selected viewport do not restart the
    renderer. In particular, entering or leaving 4:3 safe-fit on a 2.35 screen
    must not switch to a per-scene native-output rule that rebuilds the graph.
13. The loader may translate `$screen_aspect` into derived HLSL parameters,
    but the shader configuration must be able to select/parameterize behavior
    from the variable and logs must expose the selected screen aspect.
14. No backend may infer behavior from profile names such as `normal` or
    `scope`.

## NLS configuration ownership

Under unified viewport configuration, `[shaders.nls]` must not own an
independent physical-screen assumption that can disagree with the selected
viewport variables.

- `$screen_aspect` is the authoritative shader-configuration input for the
  selected physical screen.
- Shader rules may use it to select a screen-specific mapping or derive
  parameters; configuration must not require separate F2/F3-specific NLS
  shortcuts.
- `nls_target_aspect_ratio` may remain only as a documented legacy/fallback
  input when no unified viewport state is active.
- Existing `output_aspect_ratio` behavior remains a shader/media contract, not
  a mechanism for configuring madVR's physical-screen profile.
- Startup validation must reject or clearly diagnose contradictory duplicate
  ownership rather than silently choosing one value.
- Checked-in unified examples must use viewport variables for screen-aware
  shader behavior.

Illustrative syntax (the implementation may choose equivalent validated
expression/parameter syntax):

```ini
[shaders.nls]
screen_aspect: $screen_aspect
```

The resolved value is runtime state, not textual substitution performed by the
user and not a value sent to madVR.

## Configuration migration

Rename unified viewport keys:

| Existing key | Generic key |
| --- | --- |
| `scope_screen_aspect` | `screen_aspect` |
| `scope_subtitle_fit` | `subtitle_fit` |
| `scope_subtitle_hold_seconds` | `subtitle_hold_seconds` |
| `scope_subtitle_padding_pixels` | `subtitle_padding_pixels` |

For existing installations:

1. Continue accepting the old names as deprecated aliases for a documented
   migration window.
2. Log a clear replacement warning containing the profile section and new key.
3. Reject a profile that defines both an old alias and its new key, because
   precedence would otherwise be ambiguous.
4. Do not write deprecated names into generated examples or persisted state.
5. Keep legacy non-unified configuration behavior compatible until its
   separate removal is explicitly approved.

## Persistence and observability

- Persist the selected viewport profile using the existing unified-profile
  state mechanism.
- Restore and publish the resolved viewport before shader-rule selection so
  startup cannot momentarily use the wrong screen-specific mapping.
- Log profile identity, parsed aspect text, normalized ratio, numeric aspect,
  subtitle settings, generation, renderer backend, selected NLS behavior, and
  whether a restart is required.
- OSD/status output must identify the active viewport, for example
  `Viewport: normal (16:9)` or `Viewport: scope (2.35:1)`.
- If configuration is absent, log the explicit default
  `Viewport: default (16:9)` rather than an implicit/unknown state.

## Verification

1. Parser table tests cover all required ratio, `x`, uppercase `X`, decimal,
   whitespace, malformed, zero, negative, non-finite, and range cases.
2. Schema tests accept generic viewport keys, diagnose deprecated aliases, and
   reject old/new duplicates.
3. Checked-in unified configuration examples pass startup validation and
   contain no Scope-prefixed viewport settings.
4. Profile-resolution tests prove an omitted viewport and an empty viewport
   profile both resolve to 16:9.
5. Key-selection tests prove F2/Ctrl+F9 resolve the configured 2.35 viewport
   and F3/Ctrl+F10 resolve the default 16:9 viewport.
6. Runtime-variable tests prove `$screen_aspect` and `$viewport_profile` update
   coherently for every viewport selection and survive renderer replacement.
7. NLS matrix tests cover:
   - 4:3 to default 16:9;
   - 16:9 passthrough on 16:9;
   - 4:3 preserved with safe-fit/pillarboxing on 2.35;
   - 16:9 to 2.35;
   - 1.90 to 2.35;
   - 2.35 passthrough on 2.35;
   - NLS Off/native output.
8. Restart-decision tests prove native-equivalent 16:9 does not restart,
   content changes do not restart, and 4:3 safe-fit does not cause a
   per-content media-type restart.
9. Renderer-replacement tests prove viewport variables are restored before
   shader selection.
10. Integration tests prove VP never calls a madVR configuration API as a
    consequence of viewport selection.
11. Hardware tests verify final geometry, physical curtain profiles, subtitle
    placement, OSD state, and logs on both 16:9 and 2.35 presentations.

## Acceptance criteria

- `[profiles.viewport.*]` is the sole unified-config owner of physical viewport
  aspect and subtitle-fit settings.
- Generic viewport keys replace Scope-prefixed names in all checked-in unified
  configuration and documentation.
- Missing viewport configuration resolves deterministically to 16:9.
- All documented aspect formats parse consistently through one shared parser.
- Shader configuration can read the application-owned `$screen_aspect`
  variable and restores it across renderer replacement.
- NLS mapping follows detected content aspect plus selected screen aspect
  without duplicate hard-coded screen state.
- 4:3 stretches to a selected 16:9 screen but remains geometrically 4:3 on a
  selected 2.35 screen.
- VP does not pass viewport configuration to madVR; the shared F2/F3 hotkey is
  the only coordination assumed by this story.
- Automatic content-aspect changes remain restart-free.
- Existing configurations receive a safe, diagnosable migration path.

## Dependencies and boundaries

- VP-0028 provides unified profile parsing, key selection, and persisted state.
- VP-0034 provides durable restart-free NLS mapping and output contracts.
- VP-0035 provides low-latency active-aspect publication.
- This story does not configure or query madVR, automate curtains, or control
  projector lens memories. It assumes those systems may independently consume
  the same F2/F3 event and makes only VP's selected viewport coherent and
  observable.
- Hardware validation remains necessary because unit tests cannot prove final
  madVR/projector geometry.
