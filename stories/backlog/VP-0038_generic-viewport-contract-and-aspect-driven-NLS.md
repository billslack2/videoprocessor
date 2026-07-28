# VP-0038: Generic viewport contract and aspect-driven NLS

## Status

Backlog as of 2026-07-27. This story defines the configuration and runtime
contract required to make the selected `[profiles.viewport.*]` profile the
single source of truth for physical-screen geometry, subtitle fitting, and NLS
target geometry.

No implementation branch or base has been selected. Before implementation,
perform the tracker implementation-branch gate and confirm the current VP
integration base with the developer.

## User story

As a viewer who alternates between a physical 16:9 presentation with side
curtains closed and a CIH widescreen presentation with the curtains open, I
want one viewport profile selection to configure every renderer and NLS
consistently, so source cropping, scaling, nonlinear stretch, pillarboxing,
subtitle fitting, and output aspect all use the actual active screen.

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
renderer-neutral source of screen geometry.

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
5. Preserve a useful rational form for media-type negotiation when possible:
   `16:9` remains 16:9, `2.2:1` normalizes to 11:5, and `2.35:1` normalizes to
   47:20 or an equivalent exact ratio.
6. Do not maintain separate, subtly different aspect parsers in the profile,
   DirectShow, madVR shader, and libplacebo paths.

## Required runtime contract

Introduce a renderer-neutral resolved viewport value containing at least:

- selected group/profile identity;
- normalized screen aspect as both a numeric ratio and media-type-safe
  numerator/denominator;
- subtitle-fit enabled state;
- subtitle hold duration;
- subtitle padding;
- selection generation/version.

Required behavior:

1. Resolve `[profiles.viewport.*]` outside an individual renderer backend.
   One physical key event selects the viewport once and publishes one coherent
   resolved value to the active renderer.
2. Replace the boolean `SetScreenProfile(scopeScreen, ...)` boundary with an
   aspect/settings-driven viewport API, or adapt it behind a new generic API
   until all callers migrate.
3. Both libplacebo and DirectShow/madVR consume the same resolved viewport.
   Renderer switches restore the selected viewport without requiring another
   F2/F3 press.
4. DirectShow NLS obtains its target aspect from the resolved viewport, not
   from a separate hard-coded Scope assumption.
5. NLS derives active-rectangle crop, stretch ratio, warp axis, passthrough
   mode, and negotiated output aspect from the selected viewport.
6. A 4:3 active picture targeting the default 16:9 viewport retains the
   existing approximately 1.333x NLS behavior. With NLS deliberately off,
   madVR retains normal pillarboxing.
7. A 16:9 or 1.90 active picture targeting a 2.35 viewport uses horizontal
   NLS; matching 2.35 content uses linear passthrough.
8. Content-aspect changes under one selected viewport do not restart the
   renderer. A manual viewport change may request one controlled media-type
   renegotiation only when the effective output aspect truly changes.
9. Shader code need not receive a literal `screen_aspect` token if the loader
   supplies correctly derived geometry parameters, but logs and runtime state
   must expose the selected viewport and target aspect explicitly.
10. No backend may infer behavior from profile names such as `normal` or
    `scope`.

## NLS configuration ownership

Under unified viewport configuration, `[shaders.nls]` must not own an
independent screen target that can disagree with the selected viewport.

- The resolved viewport aspect owns the runtime NLS target and output contract.
- `nls_target_aspect_ratio` and a fixed NLS `output_aspect_ratio` may remain
  only as documented legacy/fallback inputs when no unified viewport model is
  active.
- Startup validation must reject or clearly diagnose contradictory duplicate
  ownership rather than silently choosing one value.
- Checked-in unified examples must use viewport-owned geometry.

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
- Restore and publish the resolved viewport before renderer/media-type setup
  so startup cannot momentarily assume 2.35 when the persisted selection is
  16:9.
- Log profile identity, parsed aspect text, normalized ratio, numeric aspect,
  subtitle settings, generation, renderer backend, NLS target, and whether a
  restart is required.
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
6. Renderer-neutral contract tests feed the same resolved viewport to
   libplacebo and DirectShow/madVR adapters.
7. NLS matrix tests cover:
   - 4:3 to default 16:9;
   - 16:9 passthrough on 16:9;
   - 16:9 to 2.35;
   - 1.90 to 2.35;
   - 2.35 passthrough on 2.35;
   - NLS Off/native output.
8. Restart-decision tests prove native-equivalent 16:9 does not restart,
   content changes do not restart, and a real 16:9/2.35 viewport change may
   request exactly one restart.
9. Renderer-replacement tests prove the selected viewport and NLS target are
   restored before media negotiation.
10. Hardware tests verify madVR and libplacebo geometry, physical curtain
    profiles, subtitle placement, OSD state, and logs on both 16:9 and 2.35
    presentations.

## Acceptance criteria

- `[profiles.viewport.*]` is the sole unified-config owner of physical viewport
  aspect and subtitle-fit settings.
- Generic viewport keys replace Scope-prefixed names in all checked-in unified
  configuration and documentation.
- Missing viewport configuration resolves deterministically to 16:9.
- All documented aspect formats parse consistently through one shared parser.
- DirectShow/madVR NLS and libplacebo consume the same selected viewport
  contract and restore it across renderer replacement.
- NLS mapping and output negotiation follow the selected viewport without
  duplicate hard-coded screen state.
- Automatic content-aspect changes remain restart-free.
- Manual viewport changes restart only when effective output geometry changes.
- Existing configurations receive a safe, diagnosable migration path.

## Dependencies and boundaries

- VP-0028 provides unified profile parsing, key selection, and persisted state.
- VP-0034 provides durable restart-free NLS mapping and output contracts.
- VP-0035 provides low-latency active-aspect publication.
- This story does not automate curtains or projector lens memories and does
  not query madVR to confirm that an independently configured hotkey was
  received. It makes VP's selected viewport coherent and observable.
- Hardware validation remains necessary because unit tests cannot prove madVR
  media negotiation or final projected geometry.
