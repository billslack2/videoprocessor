# VP-0028 proposed renderer configuration model

> **VP-0028 side-by-side test build.** The unified runtime validates its graph
> before capture, resolves independent groups and composite keys, applies
> viewport profiles, compares effective-setting fingerprints, commits atomic
> per-config state only after a successful renderer build, and schedules
> generation-scoped refresh actions. Existing legacy syntax remains supported.
> Test with a separate `/vr_config` file; do not replace the active deployed
> configuration during acceptance testing.

`VideoProcessor.cfg` and `VideoProcessorRenderer.cfg` use the same strict,
table-driven key/type/range validation engine. They retain separate schemas and
lifecycle boundaries: main application settings are startup-only, while
profile and event expressions remain renderer-only. Sharing validation does
not permit renderer rules to mutate capture, queue, conversion, or other main
application settings.

A review-oriented HTML version is available beside the existing renderer help
as `VideoProcessorRenderer-Proposed.html`.

## Purpose

VideoProcessor uses the built-in libplacebo renderer for live streaming
sources. The configuration needs to select a small, predictable set of image
processing and display-calibration settings from trustworthy capture metadata.
It must also provide direct profile shortcuts and optional post-refresh actions
without giving those concepts unrelated special-case syntax.

The model has four independent profile groups:

1. **Input** chooses HDR/SDR processing from source metadata.
2. **Scaling** chooses scaling/deband policy from source size and cadence.
3. **Display** chooses the calibrated output contract and optional LUT.
4. **Viewport** chooses normal or scope geometry.

Each group has exactly one effective profile and an independent selection mode:
`automatic` or `manual`. A profile changes only the settings it owns. The
effective renderer settings are built in this order:

```text
[display] base
  -> input profile
  -> scaling profile
  -> display profile
  -> viewport profile
```

Group ownership prevents one profile from accidentally overriding another
group's concern. For example, an HDR input profile may set tone mapping and
peak detection, but it may not change the calibration LUT; the display profile
owns that.

## Required implementation architecture

The unified path is a typed configuration system, not another layer over the
legacy display-rule slot. Its required pipeline is:

```text
parse once
  -> validate complete graph
  -> load one selection mode per group
  -> evaluate source/key event
  -> resolve typed settings by group ownership
  -> compare effective-settings fingerprint
  -> apply successfully
  -> persist committed manual selections
```

The parser must produce a platform-independent `RendererProfileConfig`.
Expressions must be parsed into one reusable AST. A `ProfileSelectionSet`
stores one independent automatic/manual selection per group. A resolver returns
both the effective typed settings and a stable fingerprint; renderer rebuild
decisions compare this result rather than profile labels or legacy rule names.

Parsing, validation, selection, persistence decisions, and event scheduling
must be unit-testable without a D3D device, window, capture card, or libplacebo
runtime. The GUI may deliver a canonical key event, but it must not interpret
profile expressions or directly choose a profile.

### Best-practice alignment

The design intentionally follows established configuration-system practices:

- **Strict schemas:** unknown and duplicate fields are rejected in unified
  mode, equivalent to closed-object validation such as JSON Schema
  `additionalProperties=false` and strict API field validation.
- **One public contract:** one documented, unversioned configuration grammar
  is validated consistently by the main and renderer schemas.
- **Single source of truth:** one parsed expression AST drives validation,
  shortcut discovery, source evaluation, and key-event evaluation.
- **Determinism:** ordered group/profile declarations and typed tie-breaking
  avoid container-order or filesystem-order behavior.
- **Transactional state:** requested state is committed and persisted only
  after successful application, with atomic file replacement.
- **Separation of concerns:** pure configuration/resolution code is isolated
  from UI dispatch, renderer resources, and delayed external actions.
- **Observable operation:** stable fingerprints and transition identities make
  decisions reproducible from logs.

Reference points include
[JSON Schema object validation](https://json-schema.org/understanding-json-schema/reference/object),
[Kubernetes strict field validation](https://kubernetes.io/docs/reference/using-api/api-concepts/#field-validation),
[Windows application accelerator semantics](https://learn.microsoft.com/windows/win32/menurc/keyboard-accelerators).

## Terms

**Profile** is a named partial set of settings in one group.

**Automatic profile** has a source-match branch in `when:`. For a group in
automatic mode, the matching profile with the highest `priority` wins; equal
priorities use the greatest number of source comparisons in the matching
branch, then the profile order declared by that group. `$key` comparisons never
increase automatic specificity.

**Key-selected profile** has an equality comparison against `$key` in its
`when:` expression. A matching key event selects that profile manually only if
the complete expression is true. No separate shortcut key or impossible
condition such as `$width==0` is needed.

**Default-only profile** has no `when:` expression. It may be the configured
startup profile for its group but is never selected by source or key evaluation.

**Event action** observes a completed renderer/display event. It may run a
configured command after a bounded delay. It cannot select a profile.

## General and group selection

`[general]` holds renderer-session policy rather than profile settings. The
initial proposal puts the persistence default, refresh-switch policy, default
event delay, and diagnostic-only choices here. It deliberately does not expose
the state-file path: it remains the fixed `VideoProcessorRenderer.state` beside
the executable.

```ini
[general]
# Default for every profile group; an individual group may override it.
persist_profile_selection: true
switch_refresh_rate: true
event_action_delay_seconds: 5
output_diagnostics: OFF
diagnostic_disable_shader_cache: OFF
```

The four built-in groups have a fixed order: `input`, `scaling`, `display`,
then `viewport`. Each `[profile_groups.<name>]` section declares that group's
ordered profile list, startup selection, and optional persistence override.
Explicit profile lists preserve reviewable declaration order and make orphaned,
duplicate, and cross-group profile sections validation errors. A `default` of
`auto` enables automatic selection; a profile name starts that group in manual
mode.

```ini
[profile_groups.input]
profiles: sdr,pq_hdr,hlg_hdr,lldv_like
default: auto
# A group-level key condition returns this group to automatic selection.
when: $key=="Ctrl+F4"

[profile_groups.scaling]
profiles: sd_hd,mid,uhd_native
default: auto
when: $key=="Ctrl+F5"

[profile_groups.display]
profiles: rec709_projector,bt2020_projector
default: rec709_projector
persist_profile_selection: true

[profile_groups.viewport]
profiles: normal,scope
default: normal
persist_profile_selection: true
```

An automatic group with no matching profile contributes no group override;
`[display]` remains in effect for that group. If persistence is enabled, a
manual selection persists across source changes and restarts until another
key-selected profile or that group's `when:` condition is invoked. If
persistence is disabled, manual selection lasts only for the current process.
Removed or invalid persisted profiles fall back to the configured startup
selection and emit one diagnostic. The global default is `true`; every group
may explicitly set `persist_profile_selection: false` when it should always
start from its configured default.

Selection precedence at startup is deterministic:

1. a valid persisted manual profile, when persistence is enabled;
2. the group's configured `default`;
3. no override if `default: auto` and no automatic profile matches.

The state file is deliberately unversioned and stores only committed manual
group selections:

```ini
screen_profile: scope
profile.display: rec709_projector
profile.viewport: scope
```

Session-only and automatic selections are not stored. For the default
`VideoProcessorRenderer.cfg`, VP continues using
`VideoProcessorRenderer.state` and preserves/writes the compatible
`screen_profile:` mirror so returning to a legacy build does not lose viewport
state. An explicit `/vr_config X.cfg` uses a sibling `X.state`; for example,
`VideoProcessorRenderer.vp0028-test.cfg` uses
`VideoProcessorRenderer.vp0028-test.state`. This isolates side-by-side tests
without exposing an arbitrary state path in configuration.

State writes use a temporary file and atomic replacement. A key request is
persisted only after the new effective configuration has applied successfully;
a failed rebuild leaves the prior active and persisted selection unchanged.

## Profile syntax and ownership

Profiles use `[profiles.<group>.<name>]`. The built-in group order and each
group's explicit `profiles:` list make selection independent of file-section
ordering.

```ini
[profiles.input.pq_hdr]
when: $transfer==PQ
priority: 100
tone_mapping: spline
gamut_mapping: perceptual
peak_detection: high_quality
contrast_recovery: 0.25

[profiles.display.rec709_projector]
# This is a manual display selection, not an impossible source condition.
when: $key=="F5"
sdr_target_primaries: REC709
sdr_target_nits: 100
sdr_black_nits: 0
output_range: FULL
output_gamma: AUTO
lut: lut\Projector-Rec709.cube
lut_reference_primaries: REC709
lut_reference_transfer: BT1886
lut_reference_range: FULL
lut_reference_nits: 100
```

Only these settings are valid in each group:

| Group | Owned settings |
| --- | --- |
| `input` | `tone_mapping`, `gamut_mapping`, `peak_detection`, `contrast_recovery`, `sdr_input_transfer` |
| `scaling` | `quality`, `upscaler`, `downscaler`, proposed `sigmoid`, proposed named `deband_strength`, `dithering` |
| `display` | target primaries/nits/black, output presentation/range/gamma, display signaling, LUT and LUT reference contract |
| `viewport` | normal/scope selection, scope aspect, subtitle-fit settings and viewport geometry |

### Structural schema

Names are case-insensitive and canonicalized to lower case. Group/profile/action
identifiers must match `[A-Za-z][A-Za-z0-9_-]{0,63}`. `auto`, `base`, `none`,
and `default` are reserved identifiers. The unified model supports exactly the four
groups `input,scaling,display,viewport` in that order.

| Section/key | Type and default | Requirement |
| --- | --- | --- |
| `persist_profile_selection` | Boolean, default `true` | global group default |
| `switch_refresh_rate` | Boolean, default `true` | session policy |
| `event_action_delay_seconds` | whole seconds `0..30`, default `5` | inherited by actions |
| diagnostic toggles | Boolean, default `false` | session policy |
| `[profile_groups.<g>] profiles` | non-empty ordered identifiers, required | no duplicates; every item has one section |
| `default` | `auto` or listed profile, required | startup selection |
| `when` | key-only expression, optional | returns this group to automatic |
| `persist_profile_selection` | Boolean, optional | overrides general policy |
| `[profiles.<g>.<p>] when` | source/key expression, optional | omitted means default-only |
| `priority` | integer `-100000..100000`, default `0` | automatic selection only |
| `[event_actions] actions` | ordered identifiers, optional | every item has one action section |
| action `on` | non-empty event list, required | allowed completed events only |
| action `when` | event expression, required | event variables only |
| action `program` | non-empty path, required | `.exe`, `.bat`, or `.cmd` |
| action `arguments` | literal string, default empty | no VP interpolation |
| action `working_directory` | path, default config directory | must exist |
| action `delay_seconds` | whole seconds `0..30`, optional | overrides general delay |

A group is **automatic-capable** when at least one listed profile has a
key-independent source branch in its ASTâ€”an expression that may evaluate true
with `$key=NONE`. Only automatic-capable groups may use `default: auto` or a
group reset condition.

### Renderer-setting schema

`AUTO` means renderer policy selection; `DEFAULT` means VP's named curated
preset. Text values are case-insensitive. Numeric bounds are inclusive except
where explicitly stated.

| Group | Key | Accepted value | Apply class |
| --- | --- | --- | --- |
| input | `tone_mapping` | `AUTO`, `spline`, `bt2390`, `st2094-40`, `reinhard` | rebuild |
| input | `gamut_mapping` | `AUTO`, `perceptual`, `softclip`, `relative`, `desaturate` | rebuild |
| input | `peak_detection` | `OFF`, `DEFAULT`, `HIGH_QUALITY` | rebuild |
| input | `contrast_recovery` | `AUTO` or decimal `0.0..1.0` | rebuild |
| input | `sdr_input_transfer` | `AUTO`, `BT1886`, `SRGB`, `1.8`, `2.0`, `2.2`, `2.4`, `2.6`, `2.8` | rebuild |
| scaling | `quality` | `fast`, `balanced`, `high` | rebuild |
| scaling | `upscaler` | `AUTO`, `ewa_lanczossharp`, `ewa_lanczos`, `bicubic`, `bilinear` | rebuild |
| scaling | `downscaler` | `AUTO`, `ewa_lanczos`, `bicubic`, `bilinear` | rebuild |
| scaling | `sigmoid` | `AUTO`, `ON`, `OFF` | rebuild |
| scaling | `deband_strength` | `OFF`, `LIGHT`, `DEFAULT` | rebuild |
| scaling | `dithering` | `AUTO`, `ON`, `OFF` | rebuild |
| display | `sdr_target_nits` | decimal `40..500` | rebuild |
| display | `sdr_black_nits` | `AUTO` or decimal `0 <= value < target` | rebuild |
| display | `output_presentation` | `AUTO`, `COMPOSED`, `DIRECT` | rebuild |
| display | `output_range` | `AUTO`, `FULL`, `LIMITED` | rebuild |
| display | `output_gamma` | `AUTO`, `BT1886`, `SRGB`, `1.8`, `2.0`, `2.2`, `2.4`, `2.6`, `2.8` | rebuild |
| display | `sdr_target_primaries` | `REC709`, `BT2020` | rebuild |
| display | `report_bt2020_to_display` | Boolean | rebuild |
| display | `lut` | empty/omitted or readable `.cube` path | rebuild |
| display | `lut_reference_primaries` | `REC709`, `BT2020` | rebuild |
| display | `lut_reference_transfer` | `BT1886`, `SRGB`, gamma values supported above | rebuild |
| display | `lut_reference_range` | `FULL`, `LIMITED` | rebuild |
| display | `lut_reference_nits` | decimal `40..500` | rebuild |
| viewport | `mode` | `NORMAL`, `SCOPE` | dynamic group transaction |
| viewport | `scope_screen_aspect` | ratio/decimal `1.5..4.0` | dynamic group transaction |
| viewport | `scope_subtitle_fit` | Boolean | dynamic group transaction |
| viewport | `scope_subtitle_hold_seconds` | decimal seconds `0..30` | dynamic group transaction |
| viewport | `scope_subtitle_padding_pixels` | integer pixels `0..500` | dynamic group transaction |

Relative LUT/program/working-directory paths resolve from the loaded renderer
configuration directory. LUT structure and reference-contract completeness are
validated before capture starts. LUT reference keys are either all present with
`lut`, or all absent.

The base `[display]` section continues to supply defaults for every setting.
Profile values are parsed into typed settings before renderer creation; the
runtime does not re-read raw strings or reuse the permissive legacy override
function. An unknown key, duplicate key/section, unsupported value, or valid
key in the wrong group is a startup validation error.

Settings also declare an apply class:

- **rebuild** settings change renderer/output resources and require a safe
  renderer reconstruction;
- **dynamic** settings, initially viewport mode only, may commit without a
  renderer rebuild when the renderer confirms success;
- **event-only** values never enter effective renderer settings.

The resolver fingerprints the typed effective values, not just selected
profile names. A source transition rebuilds only when the effective rebuild
fingerprint changes. This prevents both stale settings and repeated rebuilds
when a different rule selects equivalent values.

Application is transactional. Resolution first creates a candidate selection
and settings snapshot without mutating active state. If any changed value is
rebuild-class, VP constructs and validates one replacement renderer for all
changed groups and commits the selection only after it reaches ready state. A
dynamic viewport update passes the complete viewport group to one renderer
operation that either succeeds entirely or leaves the prior viewport intact.
Persistence and transition event publication occur after commit. Any failure
retains the previous renderer, selection set, viewport, state file, and pending
event identity.

## Strict startup validation

Unified configuration is accepted only as a complete valid graph. Validation
runs before capture or renderer startup and reports section, key, value, and
expected alternatives. It must reject:

- undeclared, duplicate, empty, or unknown groups and profiles;
- duplicate sections or keys, including case-only duplicates;
- missing profile sections, orphan profile sections, and invalid defaults;
- malformed expressions, unavailable variables, and invalid priorities;
- unknown settings, wrong-group settings, and unsupported values;
- duplicate key chords after canonicalization and unregistrable accelerators;
- group reset conditions on groups that cannot enter automatic mode;
- mixed legacy and unified selection/action sections.

No rule variable may validate unless the runtime can supply it in that
evaluation context. Future variables are added together with parsing, runtime
lookup, documentation, and tests; they are not accepted as silent placeholders.
New unified sections use strict unknown-field behavior because silently ignored
configuration is more dangerous than a startup error for a live streaming
renderer.

## Source conditions

Automatic `when:` expressions may use only stable source facts. Names and text
values are case-insensitive. Existing Boolean and comparison operators remain:
`==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`, and parentheses. Every
comparison names its variable explicitly; the legacy single-`|` value shorthand
is not accepted in unified mode.

| Variable | Values / meaning |
| --- | --- |
| `$transfer` | `SDR`, `PQ`, `HLG`, `HDR`, `UNKNOWN` |
| `$primaries` | `REC709`, `BT2020`, `P3_D65`, etc. from capture metadata |
| `$range` | `FULL`, `LIMITED`, `UNKNOWN` |
| `$cadence` | Exact named capture cadence, such as `23.976`, `24`, `25`, `29.97`, `30`, `50`, `59.94`, `60` |
| `$width`, `$height`, `$resolution` | Capture raster, for example `3840x2160` |
| `$scan` | `PROGRESSIVE` or `INTERLACED` |
| `$format` | Capture pixel format, such as `V210`, `UYVY`, `R10B`, or `R12L` |
| `$hdr_metadata` | `true` only for valid static HDR metadata |
| `$key` | Transient hotkey chord for a key-selection event, for example `"F5"` or `"Ctrl+F4"`; otherwise `NONE` |

All source variables come from one immutable source snapshot used for the
entire resolution. `$cadence` is a typed rational rate, not a binary
floating-point comparison and not a truncated integer. `23.976` and
`24000/1001` are accepted aliases for the canonical `24000/1001` family;
`29.97`/`30000/1001` and `59.94`/`60000/1001` follow the same rule.
Diagnostics print the friendly decimal plus the rational value.

Source mastering data, MaxCLL, and MaxFALL are passed to libplacebo for color
mapping. They are deliberately not ordinary profile selectors: transient or
bad metadata must not rebuild the renderer repeatedly.

Do not use filename, title, codec, bitrate, container, or media-library data:
the streaming capture path may not have it, and it is not a reliable image
processing signal. Do not use output refresh in `when:`; it is the result of
profile selection and display switching.

## Key conditions and persistence

`$key` is a transient rule variable. In ordinary source-state evaluation it is
`NONE`. When a configured hotkey arrives, VP evaluates the same parsed AST with
`$key` set to its canonical chord and all source variables set from the current
immutable source snapshot. Key chords are quoted because they may contain `+`.
This lets a profile be automatic, manual, or both without a second shortcut
syntax. The parser accepts function keys, letters, numbers, `Escape`, `Enter`,
and optional `Ctrl`, `Alt`, and `Shift` modifiers.

`$key` supports only equality against a quoted literal chord. `!=`, ranges,
wildcards, computed key names, and key-to-key comparisons are invalid. The AST
must expose all referenced chords so the GUI can register accelerators without
implementing a second expression grammar. Auto-repeat is suppressed or
debounced so holding a key cannot schedule repeated rebuilds. Failure to
register a configured chord is a startup error with the chord and owning
section in the diagnostic.

These are application-local Windows accelerators, preserving existing VP
behavior; they are not system-wide `RegisterHotKey` bindings. They are active
only while VP's main UI thread is processing keyboard messages for the active
VP window. They do not fire while another application has focus. Dialog/control
message routing must preserve existing F2â€“F6 behavior. VP emits at most one
selection request per physical key press and ignores repeat-generated command
messages until the matching key-up is observed.

```ini
[profiles.display.rec709_projector]
when: $key=="F5"

[profiles.display.bt2020_projector]
when: $key=="F6"

[profiles.viewport.normal]
when: $key=="F2"

[profiles.viewport.scope]
when: $key=="F3"

# Either a PQ stream automatically selects this input profile, or Ctrl+F7
# manually selects it and makes it persistent according to group policy.
[profiles.input.pq_hdr]
when: $transfer==PQ || $key=="Ctrl+F7"
```

`when:` in `[profile_groups.<name>]` is evaluated for key events only and must
contain only `$key` equality joined by `||`. When it matches, it returns that
one group to automatic selection. A key selection always directly selects its
target and never toggles. One canonical chord may select one profile in each
independent group and may reset one or more groups, allowing deliberate
composite input/scaling/display/viewport choices. Two matching profiles in the
same group remain a startup error rather than a priority contest.
The complete target profile expression is still evaluated, so
`$transfer==PQ && $key=="Ctrl+F7"` cannot select the profile for an SDR source.
The OSD/log reports the chord, group, prior selection, requested selection,
committed selection, and automatic/manual origin.

This replaces the released special cases without adding a separate binding
section:

| Released configuration | Proposed equivalent |
| --- | --- |
| `shortcut=F5` in `[display_rules.rec709]` | `when: $key=="F5"` in `[profiles.display.rec709_projector]` |
| `screen_profile_scope=F3` in `[shortcuts]` | `when: $key=="F3"` in `[profiles.viewport.scope]` |
| `display_rules_auto=F4` in `[shortcuts]` | `when: $key=="F4"` in the appropriate `[profile_groups.<name>]` section |

## Refresh-transition event actions

Event actions are separate from profiles. They can observe only a completed
event, so a command never participates in renderer selection or runs per
frame.

```ini
[event_actions]
actions: audio_delay_24,audio_delay_60,restore_audio

[event_actions.audio_delay_24]
on: refresh.applied,refresh.confirmed
when: $actual_refresh==23.976 || $actual_refresh==24
program: C:\Videoprocessor\audio\audio_delay.bat
arguments: 315

[event_actions.audio_delay_60]
on: refresh.applied,refresh.confirmed
when: $actual_refresh==50 || $actual_refresh==59.94 || $actual_refresh==60
delay_seconds: 5
program: C:\Videoprocessor\audio\audio_delay.bat
arguments: 285

[event_actions.restore_audio]
on: refresh.restored
when: $actual_refresh==60
delay_seconds: 0
program: C:\Videoprocessor\audio\audio_delay.bat
arguments: 0
```

Supported events are:

| Event | Meaning |
| --- | --- |
| `refresh.applied` | VP requested a refresh change and Windows reported the resulting current rate. |
| `refresh.confirmed` | VP found the requested refresh was already active; no mode change was made. |
| `refresh.restored` | Renderer shutdown/restoration successfully returned the display to VP's captured prior rate. |

There is no event for a failed or unsupported refresh request. Such failures
are logged and run no action. Every completed transition carries a monotonically
increasing transition ID and the renderer generation that requested it. VP
schedules at most one instance of each action for that `(generation,
transition, event)` identity.

The scheduler lives in the output/session coordinator rather than the render
loop or renderer object. This lets a `refresh.restored` action finish after the
renderer has released graphics resources. A pending applied/confirmed action is
cancelled when its generation is discarded or a newer transition supersedes
it. A restoration action is cancelled only by process shutdown or a newer
successful restoration for the same output.

`$actual_refresh` is a typed rational rate read after an applied change or
restoration, or the already-active rate for `refresh.confirmed`. The same
canonical aliases used by `$cadence` apply; matching never uses truncated
integers or direct binary floating-point equality. `$requested_refresh` and
`$previous_refresh` are diagnostic event facts and may be added later; they are
not required for the initial syntax.

`program:` is a required trusted local path. `arguments:` is an optional literal
argument string and `working_directory:` optionally selects an existing
directory; its default is the renderer configuration directory. VP performs no
source-variable substitution and no environment-variable expansion of its own.
An `.exe` is launched directly with Windows process creation. A `.bat` or
`.cmd` is launched through `%ComSpec% /d /s /c` with the script path quoted as
one token; other extensions are rejected. The inherited process environment is
unchanged. Event actions never run from the render thread.

VP logs the action name, event, canonical rate, delay, process ID, and
exit/start failure, while avoiding program/argument duplication in normal
diagnostics. Unified mode has no free-form `command=` key; that key remains
legacy-only.

Configuration text is UTF-8 and process paths/arguments are converted to
Unicode for `CreateProcessW`. Due actions are launched in the order listed by
`[event_actions] actions:`; the scheduler does not wait for one child before
starting the next. A background watcher may log exit status, but VP does not
terminate child processes on timeout and child lifetime is independent after
launch. With
`switch_refresh_rate: false`, VP performs no refresh transaction and emits no
`refresh.applied`, `refresh.confirmed`, or `refresh.restored` event.

## Operational diagnostics

At startup VP logs the configuration mode, source file, groups,
profiles, canonical key bindings, persistence policy, and legacy compatibility
status. On every source or key evaluation it logs one compact effective
selection:

```text
profiles input=auto:pq_hdr scaling=auto:uhd_native
         display=manual:rec709_projector viewport=persisted:scope
```

Diagnostics distinguish `base`, `automatic`, `default`, `manual`, and
`persisted` origins. They include the effective-settings fingerprint and the
specific groups whose values changed. Key requests log requested, rejected, and
committed states separately. Event diagnostics include generation and
transition IDs so delayed work can be traced and deduplication verified in
`C:\logs\vp_debug.log`.

## Streaming policy

The initial profile library should remain small:

```text
input:   sdr | pq_hdr | hlg_hdr | lldv_like
scaling: sd_hd | mid | uhd_native | optional manual performance diagnostic
display: rec709_projector | bt2020_projector
viewport: normal | scope
```

For the initial implementation, retain curated renderer choices rather than
exposing every libplacebo option:

- Tone mapping: `AUTO`, `spline`, `bt2390`, `st2094-40`, `reinhard`.
- Gamut mapping: `AUTO`, `perceptual`, `softclip`, `relative`, `desaturate`.
- Peak detection: proposed `OFF`, `DEFAULT`, `HIGH_QUALITY`.
- Scaling: existing quality presets and scaler names.
- Debanding: proposed `OFF`, `LIGHT`, `DEFAULT` mapped to validated presets.
- Sigmoid: proposed `AUTO`, `ON`, `OFF`, active only for SDR upscaling.

Do not expose raw tone/gamut constants, custom shader hooks, frame mixing,
inverse tone mapping, libplacebo deinterlacing, or subjective brightness/hue
controls in this story. They add risk or latency without a clear streaming
configuration benefit.

## Comparison with madVR and libplacebo practice

The useful madVR ideas are retained: named profiles, automatic rules, manual
selection, independent areas of concern, and persistent user choices. VP makes
the model more reviewable for a headless/text-config streaming workflow:

- group and profile order is explicit rather than implied by a UI tree;
- stable capture facts replace filename, title, codec, and library metadata;
- group ownership replaces broad profile inheritance;
- one effective selection is logged for every group;
- invalid or ambiguous configuration fails before streaming starts.

The useful libplacebo boundary is also retained: configuration selects a
curated, typed set of renderer policies, while libplacebo receives one immutable
effective settings snapshot at a safe application boundary. Raw libplacebo
structures, transient metadata, and per-frame tuning are not exposed as rule
state. New libplacebo controls enter VP only with a stable user-level meaning,
validated range/preset, group owner, apply class, diagnostics, and tests.

## Migration and review boundary

The presence of any unified markerâ€”`[general]`,
`[profile_groups.<name>]`, `[profiles.*]`, or `[event_actions]`â€”selects strict
unified mode. A file with no unified marker uses the released legacy parser and
behavior. The two modes have separate typed adapters and do not feed
half-translated records into one another. Configuration assignments use
`key: value`; legacy `key=value` files remain readable.

In unified mode, `[display_rules]`, rule `shortcut=`, `[shortcuts]`, and
`[refresh_rate_commands]` are startup errors with migration guidance. In legacy
mode, `[profile_groups.<name>]`, `[profiles.*]`, and `[event_actions]` are
startup errors. This avoids ambiguous precedence and makes a configuration
reviewable without knowing hidden compatibility ordering.

Legacy mode remains unchanged for at least one documented compatibility
release. It retains truncated refresh-command matching and existing F2/F3/F4
behavior. VP logs one compatibility notice, but it does not rewrite the file,
silently reinterpret a key, or change persistence. A separate migration guide
shows mechanical equivalents only when a legacy record's settings have one
unified group owner:

- `[display_rules]` entries become profiles in the group that owns their keys;
- rule `shortcut=` becomes a `$key` equality branch;
- fixed screen shortcuts become viewport profile key branches;
- `display_rules_auto` becomes the chosen group's reset condition;
- `[refresh_rate_commands]` becomes explicitly named event actions.

A legacy display rule that changes settings owned by multiple groups cannot be
mechanically converted without changing its one-key semantics. The unified
model does not add a coordinated multi-group preset. Those configurations require
operator redesign into independent selections or remain in legacy mode during
the compatibility period.

An event action without `delay_seconds:` inherits
`[general] event_action_delay_seconds`; a per-action value overrides it.

Legacy configuration is never rewritten automatically. A migration utility, if
added later, writes a new side-by-side file and never replaces the active file.

## Required test and acceptance matrix

Production replacement remains gated on the full acceptance matrix below.
The supplied legacy port is parser-validated and ready for isolated
side-by-side runtime testing:

| Area | Required cases |
| --- | --- |
| Parsing | ordered lists, duplicates, orphan sections, invalid defaults, unknown/wrong-owner keys, unavailable variables |
| Resolution | no match, priority, specificity, declared-order tie, equivalent-settings fingerprint |
| Keys | canonicalization, source/key `&&` and `||`, cross-group composite chords, same-group conflicts, group reset, auto-repeat/no-op |
| State | global/group policy, valid/stale entries, legacy viewport import, atomic write, failed-apply rollback |
| Transitions | automaticâ†’automatic, automaticâ†’manual, manualâ†’manual, manualâ†’automatic independently per group |
| Viewport | dynamic normal/scope application, persistence, coexistence with display/input selections |
| Events | applied/confirmed/restored, exact rational families, delay inheritance, dedupe, cancellation, teardown |
| Compatibility | unchanged legacy behavior, strict mixed-mode rejection, side-by-side migration |

Integration testing must use `/vr_config` with a side-by-side configuration and
must never overwrite the deployed active configuration. Runtime evidence comes
from `C:\logs\vp_debug.log`.

Golden resolver scenarios must assert exact outputs, not only broad coverage:

1. **PQ UHD startup:** source `PQ/BT2020/3840x2160`, no state, complete sample
   selects `input=auto:pq_hdr`, `scaling=auto:uhd_native`,
   `display=default:rec709_projector`, `viewport=default:normal`; state is
   unchanged.
2. **Independent manual display:** pressing `F6` changes only
   `display=manual:bt2020_projector`; after successful rebuild the state contains
   `profile.display=bt2020_projector` and the other three selections/fingerprint
   components remain unchanged.
3. **Session diagnostic rollback:** pressing `Ctrl+F8` selects
   `scaling=manual:performance` without writing state; pressing `Ctrl+F5`
   returns scaling to the source-selected profile without changing display or
   viewport.
4. **Failed apply:** a candidate LUT that fails validation produces no commit,
   no state write, and no completed refresh event; logs show requested,
   rejected, and retained fingerprints.
5. **Refresh event:** canonical actual `24000/1001` after a successful applied
   transition schedules `audio_delay_cinema` once for that generation and
   transition; a repeated notification with the same identity is suppressed.

Review this proposal together with the focused
`docs/examples/VideoProcessorRenderer.unified.minimal.proposed.cfg` and complete
`docs/examples/VideoProcessorRenderer.unified.proposed.cfg`. Documentation and
parser/resolver tests are accepted before more renderer/UI integration is added.
