# VP-0079 configuration model

VP-0079 uses one `VideoProcessor.cfg`, organised by the component that owns a
setting. It replaces profile registries, disk-persisted profile selections,
the separate shader shortcut grammar, and the refresh-action registry.

The exact root section is the baseline when it exists. If a domain has only
named sections, the first one declared is its startup baseline and every later
section overlays it. There is no `profiles:`, `default:`,
`persist_selection:`, `clear_when:`, `off_when:`, or state file.

## Ownership

| Section | Owner |
| --- | --- |
| `[general]` | Application startup, capture, and detection choices. |
| `[renderer_alias]` | Readable aliases for renderer-combo indexes. |
| `[queue]` | VP queue policy for either renderer. |
| `[directshow]`, `[directshow.conversion]`, `[directshow.ppm]` | DirectShow timing and conversion policy. |
| `[vprenderer]` | VideoProcessor Renderer (Alpha) base and variants. |
| `[vprenderer.viewport]` | Alpha viewport and subtitle layout. It is not shared. |
| `[shader.<group>]` | Shared shader group, with HLSL and GLSL alternatives. |
| `[actions.<name>]` | Command run after a committed event. |
| `[shortcuts]` | Fixed VP commands only. |

`[renderer_alias]` is only an index mapping; it does not own renderer
settings. `vp: 1` and `madvr: 2` map readable labels to the configured
renderer order.

## Variants and selection

```ini
[queue]
when: $key=="l"
queue_size: 32
lead_frames: 4
target_frames: 3

[queue.low_latency]
when: $key=="L"
queue_size: 1
target_frames: 1
```

The exact root is the baseline. Pressing `L` selects `low_latency`; pressing
`l` selects the root again. The manual choice lives in memory across Alpha
rebuilds and refresh transitions, then disappears when VP exits. A one-frame
queue is deliberately not the default: an underpowered renderer can trigger
frequent reset/recovery work.

`when:` uses the normal expression language. It may match source values,
`$key`, or both. Evaluation occurs on a source state change, rebuild, or key
event—not for every video frame. A child selected by `$key` becomes the
process-local manual choice; automatic source matches are reevaluated when
their source values change.

[vprenderer.rec709]
when: $key=="F4"
quality: high
sdr_target_primaries: REC709

[vprenderer.bt2020]
when: $key=="F5"
sdr_target_primaries: BT2020
report_bt2020_to_display: true
```

Because `rec709` is declared first, it is active at startup and is also
selected by `F4`. `F5` selects `bt2020`; its omitted values inherit from
`rec709`. Repeated F4/F5 selections therefore reconstruct either complete
effective configuration rather than accumulating changes.

## DirectShow

```ini
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
ppm: AUTO
```

`ppm` is a source-timing correction, applied consistently at every source
cadence. It accepts either a fixed integer or `AUTO`, which selects the
existing internal automatic-calibration sentinel. VP-0079 deliberately
replaces the old nominal-refresh map with this one policy value.

## Shaders

There is no blank `[shader]` section and no `enabled: false` value. An empty
group root is the off/baseline state.

`type: single` is the default and should normally be omitted. A direct single
section represents one effect. `shader_type:` identifies the effect itself so
that `type:` can mean **single versus multi group**.

```ini
[shader.nls]
when: $key=="n"

[shader.nls.standard]
when: $key=="Shift+n"
shader_type: nls
hlsl_file: NLS.hlsl
glsl_file: NLS.glsl
stage: pre_resize
order: 10

[shader.nls.protected]
when: $key=="Shift+p"
shader_type: nls
hlsl_file: NLS.hlsl
glsl_file: NLS.glsl
stage: pre_resize
order: 10
geometry: protected
center_protection: 0.35
```

The `shader.nls` root is empty: `n` returns NLS to off, `Shift+n` selects
`standard`, and `Shift+p` selects `protected`. The children are mutually
exclusive because the group is single by default.

Use `type: multi` only for a real composition. Every child that matches a
single key or source state is included. `stage` and `order` give the final
chain deterministic order.

```ini
[shader.cleanup]
type: multi
when: $key=="d"

[shader.cleanup.deband]
when: $key=="D"
shader_type: custom
stage: pre_resize
order: 30
hlsl_file: Debanding mild.hlsl

[shader.cleanup.sharpen]
when: $key=="D"
shader_type: custom
stage: post_resize
order: 10
hlsl_file: Adaptive sharpen.hlsl
```

`D` selects both cleanup children; `d` returns that group to empty.
Independent groups compose, so an automatic `[shader.broadcast_sdr]` effect
can coexist with manual NLS. A missing `hlsl_file` or `glsl_file` is valid:
that logical shader is ignored by the renderer that has no implementation for
it.

## Actions

Actions are discovered directly—there is no list section. `run:` is one
command line: the executable or script first, then its arguments. The default
scope is `vprenderer`; `directshow` and `*` are accepted scopes for the
matching event producer.

```ini
[actions.audio_delay_film]
on: refresh.applied,refresh.confirmed
when: $actual_refresh<=30
run: C:\Videoprocessor\audio\audio_delay.bat 100
```

The current committed events are `refresh.applied`, `refresh.confirmed`, and
`refresh.restored`. Their supported variables are `$actual_refresh`,
`$requested_refresh`, and `$previous_refresh`. Actions are evaluated at the
completed transition and never per frame.

## Complete example

The repository sample is the current review configuration:
[VideoProcessor.cfg](../VideoProcessor.cfg). The generated HTML reference is
[CONFIGURATION.html](../CONFIGURATION.html).
