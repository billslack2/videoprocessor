# VP-0045: Namespace built-in renderer configuration as `vpvr`

## Status

Backlog. No implementation has started.

## User story

As a VP user reading one shared configuration file, I want every setting owned
by the built-in renderer grouped under a clear `vpvr` namespace, so it is
obvious which settings do not configure madVR or core capture behavior.

## Requested configuration shape

Rename built-in-renderer sections to the `vpvr` namespace. Examples:

```ini
[profile_groups.viewport]
profiles: normal,scope

[profiles.viewport.scope]
when: $key=="F2"
screen_aspect: 2.35:1

[vpvr.display]
sdr_target_nits: 100

[vpvr.general]
switch_refresh_rate: true
```

Apply the same ownership rule to all built-in renderer display-rule sections:

```ini
[vpvr.display_rules]
[vpvr.display_rules.<rule>]
```

The unprefixed renderer sections (`[display]`, `[general]`, and
`[display_rules*]`) must no longer appear in checked-in examples or generated
HTML documentation. Shared profile sections remain unprefixed.

## Ownership boundary

Do not namespace shared application settings merely because they are near the
renderer configuration. These remain outside `vpvr`, including
`[command_line]`, `[queue_recovery]`, `[shortcuts]`, `[lldv]`,
`[p010_conversion]`, `[ppm_correction]`, `[shaders*]`, `[event_actions*]`,
logging settings, `[profile_groups.*]`, and `[profiles.*]`.

The profile/viewport parser publishes resolved viewport state to both Alpha and
DirectShow/NLS. Its raw configuration remains shared and unprefixed so neither
renderer needs a duplicate viewport/profile section.

Checked-in examples and HTML documentation must list all shared sections before
the `vpvr` block. Within the `vpvr` block, group renderer-only sections together
so their ownership is visually obvious.

## Migration and compatibility

1. `vpvr.display`, `vpvr.general`, and `vpvr.display_rules*` are the canonical
   documented built-in-renderer schema.
2. Continue accepting each unprefixed former renderer-only section as a
   deprecated alias for
   one documented migration window.
3. If canonical and legacy versions of the same logical section/key are both
   present, reject or clearly diagnose the ambiguity; do not silently merge
   conflicting values.
4. Log a section-specific migration warning when a legacy section is consumed,
   naming its `vpvr.*` replacement.
5. Do not automatically rewrite an active user configuration on startup.
   Deployment/config migration must back up the file, preserve unrelated
   comments and values, and make the smallest safe section rename only when
   explicitly requested.
6. Keep any already-supported historical `[libplacebo]` fallback behavior
   compatible during this migration, but do not present it as the preferred
   user-facing name.

## Implementation requirements

1. Centralize canonical/legacy section resolution rather than scattering
   string fallbacks across the built-in renderer, profile runtime, shortcuts,
   and display-rule loader.
2. Update all built-in-renderer reads, validation, warning text, display-rule
   lookup, and renderer-specific shortcut discovery to use the canonical
   namespace.
3. Do not change profile persistence or the shared profile parser. Existing
   `VideoProcessor.state` values and shared profile section names remain valid.
4. Update sample configuration, HTML help, parser/schema tests, and any
   configuration diagnostics to use `vpvr` consistently.
5. Retain current rendering behavior for display profiles, viewport F2/F3
   selection, screen aspect, subtitle fit, refresh switching, LUTs, output
   signaling, shader rules, and Alpha/madVR handoff.

## Verification

1. Parse a canonical `vpvr.*` configuration and confirm all built-in renderer
   settings resolve identically to the current unprefixed schema.
2. Parse legacy-only configurations and confirm compatibility plus one precise
   migration warning per consumed logical section.
3. Test canonical/legacy duplicates and conflicting settings; assert a clear,
   deterministic failure/diagnostic.
4. Test unchanged shared profile resolution, state persistence, F2/F3 viewport
   selection, and DirectShow/Alpha consumption of the shared viewport state.
5. Test `vpvr.display_rules*` automatic and manual selection, including
   refresh switching, LUT/output settings, and F4 automatic selection.
6. Verify unprefixed unrelated application sections and shader rules retain
   their existing behavior.
7. Build the x64 Release configuration and validate the sample configuration
   and HTML help contain no canonical unprefixed built-in-renderer sections.

## Acceptance criteria

- The checked-in canonical schema uses `vpvr.display`, `vpvr.general`, and
  `vpvr.display_rules*`, while shared profile sections remain unprefixed above
  the `vpvr` block.
- The actual ownership of built-in renderer configuration is obvious from the
  section name.
- Existing users receive safe, diagnosable compatibility rather than silent
  loss of renderer/profile settings.
- Shared viewport state remains unprefixed and works for both Alpha and
  DirectShow/NLS without duplicated configuration.
- No unrelated VP or madVR configuration is renamed or changed.

## Readiness

This is mechanically modest but cross-cutting: configuration parsing,
profile/runtime resolution, shortcut discovery, display rules, tests, examples,
and HTML documentation must change together. It should be implemented as a
single small migration with focused regression tests, not as an unverified
global text replacement.
