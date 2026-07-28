# VP-0045: Namespace built-in renderer configuration as `vpvr`

## Status

Backlog. No implementation has started.

Readiness review on 2026-07-28 found the naming goal sound but rejected the
original whole-section migration because it conflicted with the unified profile
model and treated mixed-ownership `[general]` as renderer-only. This revision
records the corrected design. The current GitHub default integration branch is
`v1.1.014-beta`; implementation remains gated on developer confirmation of
that base.

## User story

As a VP user reading one shared configuration file, I want the built-in
renderer's base treatment and session-policy sections grouped under a clear
`vpvr` namespace, so those settings are not mistaken for madVR or core capture
configuration.

This is a section-ownership improvement, not a new profile system. Shared
profile containers remain unprefixed even when a selected profile contributes
settings consumed by the built-in renderer.

## Canonical configuration model

The canonical schema is:

```ini
[general]
persist_profile_selection: true
event_action_delay_seconds: 5

[profile_groups.display]
profiles: rec709,bt2020
default: rec709
when: $key=="F4"

[profiles.display.rec709]
when: $key=="F5"
sdr_target_primaries: REC709

[profile_groups.viewport]
profiles: normal,scope
default: normal

[profiles.viewport.scope]
when: $key=="F2"
screen_aspect: 2.35:1

[vpvr.display]
sdr_target_nits: 100
quality: high

[vpvr.general]
switch_refresh_rate: true
output_diagnostics: false
diagnostic_disable_shader_cache: false
```

`[vpvr.display]` owns unconditional built-in-renderer treatment and output
defaults. `[vpvr.general]` owns only cross-profile built-in-renderer session
policy:

- `switch_refresh_rate`
- `output_diagnostics`
- `diagnostic_disable_shader_cache`

The shared `[general]` section remains canonical for:

- `persist_profile_selection`
- `event_action_delay_seconds`

The implementation must maintain an explicit allowlist for each of these
sections. A key in the wrong ownership domain is a validation error rather than
being silently accepted in a convenient nearby section.

## Shared ownership boundary

The following remain outside `vpvr`:

- `[command_line]`, `[queue_recovery]`, `[shortcuts]`, `[lldv]`,
  `[p010_conversion]`, `[ppm_correction]`, logging, and other core application
  sections;
- `[shaders*]`;
- `[event_actions*]`;
- `[profile_groups.*]` and `[profiles.*]`; and
- shared `[general]` keys listed above.

The profile/viewport runtime publishes resolved state to both Alpha and
DirectShow/NLS. Its raw configuration and `VideoProcessor.state` format remain
shared and unprefixed. F2/F3 viewport selection, F4 return-to-automatic display
selection, and F5/F6 display-profile selection continue through the existing
unified profile runtime.

Canonical checked-in examples and HTML documentation must list shared sections
before the `vpvr` block. Dedicated legacy fixtures or migration examples may
show deprecated names when clearly labeled as non-canonical.

## Display-rule boundary

`[display_rules]` and `[display_rules.<rule>]` are the pre-unified compatibility
model. Do not create `vpvr.display_rules*`.

Unified configuration continues to use `[profile_groups.display]` and
`[profiles.display.*]` and continues to reject legacy display-rule sections.
Legacy-only configurations may continue using unprefixed `[display_rules*]`
during their existing compatibility path. Migration guidance must direct users
from display rules to unified profile groups/profiles, not to a newly
namespaced display-rule subsystem.

The presence of `vpvr.*` alone must not change whether the shared profile
runtime considers a file unified. Existing profile/event-section detection
continues to decide unified versus legacy mode.

## Compatibility and conflict contract

### Display settings

1. `[vpvr.display]` is canonical.
2. `[display]` is its deprecated section alias.
3. `[libplacebo]` remains the historical fallback and is never documented as
   canonical.
4. If `[vpvr.display]` coexists with either `[display]` or `[libplacebo]` in the
   same resolved configuration, startup validation fails and names both
   sections. Do not merge them, even when their values are identical.
5. When `[vpvr.display]` is absent, preserve the current legacy per-key
   precedence: `[display]` first, then `[libplacebo]`.

### Renderer session-policy settings

1. `[vpvr.general]` is canonical for the three enumerated renderer-policy keys.
2. `[general]` remains a valid shared section and may coexist with
   `[vpvr.general]`.
3. A renderer-policy key in `[general]` is a deprecated alias. If the same key
   also appears in `[vpvr.general]`, startup validation fails even when the
   values are identical.
4. For compatibility when a canonical policy key is absent, preserve the
   existing legacy precedence for that key: `[general]`, then `[display]`, then
   `[libplacebo]`.
5. Shared `[general]` keys are not aliases and must never be read from
   `[vpvr.general]`.

### Diagnostics and warnings

- Conflict and unknown-section errors are deterministic startup errors before
  capture begins.
- Emit at most one migration warning per consumed deprecated logical section
  per loaded configuration path. Renderer rebuilds, profile changes, and
  optional-plugin reloads must not repeat the warning.
- The warning names the canonical replacement. Legacy display-rule warnings
  name `[profile_groups.display]`/`[profiles.display.*]`, not a nonexistent
  `vpvr.display_rules`.
- Do not automatically rewrite an active user configuration.
- Any requested deployment migration must back up the affected file, preserve
  unrelated values/comments, and make only the required section/key edits.

## Migration window

Deprecated `[display]` and renderer-policy keys in `[general]` must remain
accepted in the first published build containing VP-0045 and the immediately
following published build. Removing an alias after that minimum two-build
window requires a separate approved story with release notes and migration
evidence. Existing `[libplacebo]` and legacy display-rule compatibility are not
removed by this story.

## Implementation architecture

1. Add one renderer-domain, read-only normalized configuration view or section
   resolver. It owns canonical names, legacy resolution, conflict detection,
   key ownership, and migration-warning records.
2. Do not put VP renderer aliases into generic `ConfigFile`.
3. Use the normalized view consistently for startup/schema validation, built-in
   renderer settings, profile application, shortcut discovery where applicable,
   and legacy display-rule compatibility.
4. Preserve the existing renderer-config path selected by `--vr_config` and
   pinned across the core/optional-plugin ABI. Both module copies must resolve
   the same effective values; only startup validation emits migration warnings.
5. Update strict section ownership and unknown/orphan validation to recognize
   `vpvr.display` and `vpvr.general` in both combined `VideoProcessor.cfg` and
   renderer-sidecar configurations.
6. Preserve settings order: resolved display base, selected profile overrides,
   then cross-profile renderer session policy. Namespacing must not alter
   effective rendering values.
7. Do not change profile persistence, state-file path/content, renderer
   algorithms, queueing, timestamps, NLS transitions, or madVR behavior.

## Verification

### Parser and migration tests

1. Canonical `vpvr.display` and `vpvr.general` resolve to the same effective
   settings as the current unified sample.
2. Legacy unified `[display]`/`[general]` settings remain compatible and produce
   one precise warning per consumed deprecated logical section.
3. Canonical/legacy display-section coexistence fails, including identical
   values and empty legacy sections.
4. Canonical/legacy renderer-policy duplicate keys fail deterministically;
   coexistence with shared-only `[general]` keys succeeds.
5. `[display]` plus `[libplacebo]` retains current per-key fallback behavior
   when `vpvr.display` is absent.
6. Unknown `vpvr.*` sections, unknown keys, and keys placed in the wrong
   ownership section fail strict validation.
7. Warning de-duplication survives renderer rebuilds and optional-plugin
   configuration reloads.

### Unified and legacy behavior

1. Unified `[profile_groups.*]`/`[profiles.*]` selection and override precedence
   remain unchanged.
2. Unified configurations still reject `[display_rules*]`.
3. A legacy-only display-rule configuration still performs its existing
   automatic/manual selection without introducing `vpvr.display_rules*`.
4. F2/F3 viewport, F4 automatic display selection, and F5/F6 display profiles
   retain their current behavior.
5. Shared viewport state and `VideoProcessor.state` remain unchanged for Alpha
   and DirectShow/NLS.
6. Refresh switching, LUT/output signaling, shader rules, event actions, and
   Alpha/madVR handoff retain their current behavior.

### Configuration locations and release validation

1. Test combined `VideoProcessor.cfg`, the compatibility renderer sidecar, and
   an explicit `--vr_config` selection.
2. Build and run the x64 Release tests.
3. Validate canonical sample configuration and generated HTML use
   `vpvr.display`/`vpvr.general` and contain no canonical unprefixed renderer
   sections.
4. Allow deprecated names only in clearly labeled migration documentation and
   legacy regression fixtures.

## Acceptance criteria

- Canonical base and session-policy ownership is visibly expressed by
  `vpvr.display` and `vpvr.general`.
- Unified display selection remains `[profile_groups.display]` plus
  `[profiles.display.*]`; no canonical `vpvr.display_rules*` exists.
- Shared profile, viewport, persistence, event-action, shader, and core
  application sections remain unprefixed.
- Canonical and deprecated names have deterministic, tested coexistence and
  precedence rules with once-only migration diagnostics.
- Existing configurations retain a documented compatibility window without
  silent setting loss or automatic file rewriting.
- Effective rendering, profile persistence, viewport publication, DirectShow
  consumption, madVR behavior, and renderer-config path selection do not
  change.

## Readiness decision

The corrected design is implementation-ready after the developer confirms the
GitHub default integration branch required by the tracker gate. The work is
moderate and cross-cutting because validation occurs on startup-fatal paths and
the core and optional renderer plugin each load configuration. It must be
implemented as a focused migration with a centralized resolver and regression
tests, not as a global text replacement.
