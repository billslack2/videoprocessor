# VP-0036: Consolidate application and renderer settings into one configuration file

## Status

Done on 2026-07-27. Merged as VideoProcessor PR #11, commit
`09102a9`. The active deployment was backed up to
`C:\Videoprocessor\vp\backup-before-vp0036-20260727-212200` before its
renderer sections and state were migrated into the primary configuration.

Release x64 built successfully and the non-GPU suite passed 119/119. An active
Alpha-renderer smoke test logged one selected configuration path for both the
primary and renderer domains, `VideoProcessor.state`, plugin API 5, and
`alpha_queue_size: 1`.

## User story

As a VideoProcessor operator, I want application, capture, shader, renderer
profile, hotkey, and completed-event settings in one `VideoProcessor.cfg`, so
one coherent file describes the running system and can be backed up, reviewed,
and deployed without coordinating a sidecar renderer configuration.

## Decision

`VideoProcessor.cfg` becomes the default source for both application-owned and
renderer-owned sections. Separate renderer configuration is a compatibility
override, not the normal deployment model.

No configuration-version key is introduced. Both `key: value` and legacy
`key=value` assignments remain readable.

## Scope

- Load unified `[display]`, `[general]`, `[profile_groups.*]`,
  `[profiles.*]`, and `[event_actions.*]` sections from the file selected by
  `/config`.
- Pass the selected primary configuration path through the optional-renderer
  plugin boundary instead of having the plugin independently assume
  `VideoProcessorRenderer.cfg`.
- Retain parsing of `/vr_config <path>` only as an unadvertised compatibility
  override. When present, renderer-owned sections come from that file;
  otherwise they come from the primary file.
- Make validation ownership-aware:
  - the main schema strictly validates application-owned sections and keys;
  - the renderer schema strictly validates renderer-owned sections and keys;
  - each schema ignores sections owned by the other;
  - an aggregate startup pass still rejects sections or keys owned by neither.
- Keep persisted profile selections outside the configuration file and derive
  a deterministic state path from the selected renderer configuration source.
- Update HTML documentation and checked-in examples to show one normal
  configuration file.
- Port the active deployment to one file only after backing up both existing
  files and preserving every active value and comment with operational meaning.

## Compatibility and precedence

1. `/config` selects the primary configuration file.
2. Without `/vr_config`, both application and renderer domains use the primary
   file.
3. With `/vr_config`, only renderer-owned sections use the override file.
4. Command-line value switches retain their existing highest precedence.
5. Legacy `VideoProcessorRenderer.cfg` remains readable when selected
   explicitly; its mere presence beside the executable does not override the
   primary file.

Startup diagnostics must identify the selected primary path, renderer path,
whether a compatibility override is active, and the state path.

## Implementation notes

The current `RendererProfileConfig::Read` global section whitelist must be
split from its owned-section validation before it can safely consume a combined
file. `MainConfigSchema` has the inverse requirement. Centralizing the aggregate
unknown-section check avoids weakening strict validation.

The libplacebo plugin ABI should carry the resolved renderer configuration path
from the GUI. The plugin must not rediscover configuration independently from
its working directory.

## Verification

- Parser/schema tests for a combined file containing both main and renderer
  sections.
- Unknown main key, renderer key, and wholly unknown section rejection.
- Default single-file load and explicit `/vr_config` override tests.
- Plugin-path propagation test or diagnostic assertion.
- State-path and persistence tests for default and override modes.
- Legacy equals-assignment and separate-file compatibility tests.
- Debug and Release builds plus the complete non-GPU test suite.
- Side-by-side deployment test followed by an active one-file configuration
  smoke test covering F2/F3, F4/F5/F6, Ctrl+F9/F10, Alpha queue depth, and
  renderer restart.

## Acceptance criteria

- A normal deployment requires only `VideoProcessor.cfg` for all configuration.
- Every currently active application and renderer setting can be represented
  in that file without duplication or undocumented precedence.
- `alpha_queue_size` and other application-only settings remain inaccessible
  to renderer profile overrides.
- Strict validation remains intact across both ownership domains.
- Legacy `/vr_config` parsing remains compatible without appearing in user
  help or current documentation.
- The active deployment is ported with a recoverable backup and starts without
  configuration warnings or lost settings.
