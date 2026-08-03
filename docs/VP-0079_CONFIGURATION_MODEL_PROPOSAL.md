# VP-0079 configuration model proposal

> **Status: design for review, not current runtime syntax.** The production
> binary still accepts the older `[profile_groups.*]`, `[profiles.*]`,
> `[shaders]`, and `[event_actions]` grammar. Do not copy the examples in this
> document into a deployed configuration until the accompanying implementation
> has landed.

## Goal

VideoProcessor should have one `VideoProcessor.cfg` that is easy to read,
validate, and change. A setting should live with the component that owns it.
Selectable variants should use the same expression syntax as every other
conditional choice. There should be no duplicate list of profile names, no
hidden disk-persisted profile state, no renderer-specific side file, and no
special shortcut language for shaders.

This is a single-file model. It deliberately does **not** create separate
`render.cfg`, `directshow.cfg`, and `videoprocessor.cfg` files. Queue policy,
source state, renderer transitions, shared shader selection, and external
actions interact too closely to validate or deploy independently. A future
include mechanism may split files for packaging, but all included files must
merge and validate as one atomic configuration snapshot.

## Namespaces and ownership

Names describe the owner, not the UI renderer index.

| Namespace | Owner and purpose |
| --- | --- |
| `[general]` | Application-wide startup and capture choices. It contains no selectable profile settings. |
| `[queue]` | VP-owned queue policy, regardless of the selected renderer. |
| `[directshow]` | DirectShow pipeline/timestamp/input-conversion configuration. DirectShow is a pipeline, not a sibling of Alpha under a generic renderer namespace. |
| `[directshow.ppm]` | DirectShow source-timing PPM policy. |
| `[vprenderer]` | Alpha / VideoProcessor Renderer output, tone-mapping, scaling, and presentation configuration. |
| `[vprenderer.viewport]` | Alpha viewport and subtitle-fit configuration. It is not a shared viewport namespace. |
| `[shader]` | The common external-shader selector. A selected entry can carry HLSL and/or GLSL implementations. |
| `[actions.*]` | External commands started in response to committed VP events. |
| `[shortcuts]` | Fixed VP commands such as renderer selection, fullscreen, and reset. Profile keys are declared in profile `when:` expressions instead. |
| `[renderer_alias]` | Readable aliases for renderer indices used by configuration that must reference the UI renderer order. |

`[renderer_alias]` is not an ownership namespace. It only maps a friendly
token to an index, for example `vp: 1` and `madvr: 2`. VP must validate every
mapping against the resolved renderer order at startup and report a stale alias
as a configuration error.

## Root and variant syntax

A registered domain has an exact root section and zero or more named variants:

```ini
[queue]
when: $key=="l"
queue_size: 32
target_frames: 2

[queue.low_latency]
when: $key=="L"
queue_size: 1
target_frames: 1
```

The root is always the baseline. A named section overlays only values it owns.
There is no `profiles:` list and no `default:` key. Section declaration order
is never meaningful: the configuration reader stores sections by name.

The same model supports registered nested domains. `directshow.ppm` and
`vprenderer.viewport` are domain roots; one additional identifier is their
variant:

```ini
[directshow.ppm]
ppm: -17

[directshow.ppm.film]
when: $source_rate<=30
ppm: -17

[vprenderer.viewport]
when: $key=="F3"

[vprenderer.viewport.scope]
when: $key=="F2"
screen_aspect: 2.35:1
```

The parser identifies a variant using the longest registered domain root. It
does not implement arbitrary INI inheritance.

## Selection and rule evaluation

`when:` is the only condition language. It uses the existing expression parser
and may reference source variables, `$key`, or both.

### Automatic rule

A rule that matches source state is evaluated when VP publishes a new source
snapshot, such as an input format, rate, transfer, HDR-metadata, or effective
picture-classification change.

```ini
[directshow.ppm.film]
when: $source_rate<=30
ppm: -17
```

Automatic candidates are re-evaluated after a relevant state change. If more
than one candidate can win in one domain, the configuration must make the
winner explicit with `priority:`. An equal best match is a startup validation
error; file order is not a tie-breaker.

### Key-qualified rule

On a key event, VP evaluates the whole expression against the same immutable
source snapshot. A matching child becomes that domain's **manual in-memory
selection**.

```ini
[shader.nls_hdr]
when: $key=="N" && ($transfer=="PQ" || $transfer=="HLG")
type: nls
hlsl_file: NLS.hlsl
glsl_file: NLS.glsl
```

The source clause is an eligibility check at key-press time. The resulting
manual selection survives a renderer rebuild, refresh-rate transition, and
other process-local renderer changes. It is never written to disk and is
discarded when VP exits.

A rule may contain both automatic and key paths:

```ini
[shader.broadcast_sdr]
when: ($transfer=="SDR" && $source_rate>=59) || $key=="B"
type: custom
hlsl_file: Debanding mild.hlsl
```

The source path may select it automatically; a successful `B` key press may
select it manually. A manual selection wins over later automatic evaluation
until a different key selection or root reset replaces it.

### Root key condition

A root always supplies baseline settings. If its `when:` matches a key event,
it clears that domain's manual child selection and returns to the root baseline
and any applicable automatic child selection.

```ini
[vprenderer]
when: $key=="F4"
# baseline Alpha settings
```

No `clear_when`, `persist_selection`, global persistence default, profile-state
file, or disk restore behavior is part of this model.

### Evaluation is event-driven

Rules do not execute once per video frame. VP evaluates a domain when one of
its input values changes, when a relevant component starts/rebuilds, or when a
registered key event arrives. It then compares the resolved configuration with
the active snapshot:

```text
source/key event
  -> immutable state snapshot
  -> resolve automatic rules and manual selections
  -> compare effective values
  -> apply live changes or request a renderer rebuild only if required
```

A state change is not itself a renderer rebuild. Queue settings can apply live;
some Alpha output, presentation, and shader changes may require a rebuild. A
replacement renderer always receives the current in-memory selection.

## Shader model

The current shader grammar combines a registry, automatic matching, manual
shortcuts, backend pairing, and shader parameters. The target model removes
that separate selector engine.

`[shader]` is an intentionally valid empty baseline. It means no forced manual
shader selection; automatic shader variants may still match. A shader variant
contains its selection condition and its logical configuration. HLSL and GLSL
files are explicit alternatives for the same logical shader, so pairing does
not rely on duplicate shortcut names.

```ini
[shader]

[shader.off]
when: $key=="n"
selection: none

[shader.nls]
when: $key=="N"
type: nls
label: Nonlinear Stretch
stage: pre_resize
hlsl_file: NLS.hlsl
glsl_file: NLS.glsl
hlsl_quality: high
geometry: classic
strength: 1.0
curve: 2.0
tolerance_percent: 5
```

`selection: none` is a manual selector state, not a global `enabled: false`
switch. It intentionally suppresses automatic shader matching for the current
VP process. A missing `hlsl_file` or `glsl_file` is valid: the variant simply
has no implementation for that backend.

Typed NLS behavior—geometry, active-picture handling, output contract, and
Alpha prewarm—remains implementation-specific. Only selection becomes common.

## DirectShow PPM model

PPM correction is keyed from DirectShow source timing, not output display
refresh. Its rule expressions should therefore use `$source_rate`, not
`$actual_refresh`.

```ini
[directshow.ppm]
ppm: AUTO

[directshow.ppm.film]
when: $source_rate<=30
ppm: -17

[directshow.ppm.video]
when: $source_rate>30
ppm: -14
```

`ppm: AUTO` retains automatic calibration. Applying a newly resolved PPM value
must begin a fresh DirectShow timing epoch; VP must not change timestamp trim
mid-epoch.

## Actions

Actions are external side effects, not profiles. A profile changes VP behavior;
an action tells another program that a committed VP event occurred.

Every `[actions.<name>]` section is discovered directly. There is no
`[event_actions] actions:` list. `run:` contains the executable/script and its
arguments on one Windows command line. Relative paths resolve beside the
configuration file.

```ini
[actions.audio_delay_film]
on: refresh.applied,refresh.confirmed
when: $actual_refresh<=30
run: C:\Videoprocessor\audio\audio_delay.bat 100

[actions.signal_lost]
scope: *
on: signal.lost
run: C:\Videoprocessor\automation\signal-lost.cmd
```

`scope:` controls the event producer:

| Value | Meaning |
| --- | --- |
| `vprenderer` | Alpha / VideoProcessor Renderer events; this is the documented default. |
| `directshow` | DirectShow pipeline events. |
| `*` | Every matching scoped event. The action runs once per emission, so it may run more than once if multiple scopes publish the same event. |

The target event dispatcher publishes events only after a committed state
transition, never once per frame. Initial event families are:

* `refresh.applied`, `refresh.confirmed`, `refresh.restored`;
* `source.changed`, `picture.changed`;
* `profile.changed`;
* `renderer.started`, `renderer.stopped`, `renderer.reset`;
* `signal.lost`, `signal.restored`.

Each event has a documented immutable context. An action may only reference
variables that the event supplies. Unknown events, invalid scope values,
unknown variables, unsafe `run:` targets, and malformed command lines are
startup validation errors.

## Complete review example

The following is the intended shape of the active configuration. It preserves
the user's current policies while removing redundant registries and the stale
`automatic_crop` setting.

```ini
[general]
capture_device: Decklink Quad HDMI Recorder (2)
renderer: DirectShow - madVR
fullscreen: true
windowed_fullscreen_mode: true
startminimized: true
scene_detect: true
disable_detection_features: false
scene_correction_basic: false
hdr_colorspace: FOLLOW_INPUT_LLDV
hdr_luminance: FOLLOW_INPUT_LLDV

[renderer_alias]
vp: 1
madvr: 2

[queue]
when: $key=="l"
queue_size: 32
lead_frames: 4
target_frames: 2

[queue.low_latency]
when: $key=="L"
queue_size: 1
target_frames: 1

[shortcuts]
render.1: A
render.2: M

[directshow]
video_conversion: V210_TO_P010
renderer_start_stop_time_method: RATIONAL_RATIONAL
frame_offset: 90
renderer_nominal_range: AUTO

[directshow.conversion]
conversion_method: SIMD
min_core_count: 1
max_core_count: 2

[directshow.ppm]
ppm: -17

[vprenderer]
when: $key=="F4"
sdr_target_nits: 100
sdr_black_nits: 0
quality: high
tone_mapping: spline
gamut_mapping: perceptual
peak_detection: AUTO
contrast_recovery: AUTO
upscaler: AUTO
downscaler: AUTO
deband: AUTO
dithering: AUTO
output_presentation: DIRECT
output_range: FULL
output_gamma: AUTO
sdr_input_transfer: AUTO
sdr_target_primaries: REC709
report_bt2020_to_display: false
switch_refresh_rate: true

[vprenderer.rec709]
when: $key=="F5"
sdr_target_primaries: REC709
report_bt2020_to_display: false

[vprenderer.bt2020]
when: $key=="F6"
sdr_target_primaries: BT2020
report_bt2020_to_display: true

[vprenderer.viewport]
when: $key=="F3"

[vprenderer.viewport.scope]
when: $key=="F2"
screen_aspect: 2.35:1
subtitle_fit: true
subtitle_hold_seconds: 2
subtitle_padding_pixels: 30

[shader]

[shader.off]
when: $key=="n"
selection: none

[shader.nls]
when: $key=="N"
type: nls
label: Nonlinear Stretch
stage: pre_resize
hlsl_file: NLS.hlsl
glsl_file: NLS.glsl
hlsl_quality: high
geometry: classic
strength: 1.0
curve: 2.0
tolerance_percent: 5

[shader.nls_protected]
when: $key=="P"
type: nls
label: Nonlinear Stretch Protected
stage: pre_resize
hlsl_file: NLS.hlsl
glsl_file: NLS.glsl
hlsl_quality: high
geometry: protected
strength: 1.0
center_protection: 0.35
curve: 2.0
tolerance_percent: 5

[shader.broadcast_sdr]
when: $transfer=="SDR" && ($source_rate==59 || $source_rate==60)
type: custom
stage: pre_resize
hlsl_file: Debanding mild.hlsl

[actions.audio_delay_film]
on: refresh.applied,refresh.confirmed
when: $actual_refresh<=30
run: C:\Videoprocessor\audio\audio_delay.bat 100

[actions.audio_delay_video]
on: refresh.applied,refresh.confirmed
when: $actual_refresh>=50
run: C:\Videoprocessor\audio\audio_delay.bat 65
```

## Required implementation work

The current profile runtime provides useful pieces—strict INI parsing,
expression compilation, immutable snapshots, key dispatch, and change
fingerprints—but it is still hard-coded around profile groups and legacy
shader/action paths. The implementation must:

1. discover registered domain roots and their variants, including dotted roots;
2. resolve root plus selected variant values through one central runtime;
3. keep manual selections only in memory across renderer transitions;
4. move DirectShow PPM and shader selection onto the common resolver;
5. have renderers consume a resolved snapshot rather than independently
   re-reading raw configuration;
6. replace the refresh-only Alpha action path with a scoped VP event dispatcher;
7. strictly validate every section, value, expression, alias, scope, and
   command target before capture starts.

Until that work is complete, the current production configuration remains the
only supported grammar.
