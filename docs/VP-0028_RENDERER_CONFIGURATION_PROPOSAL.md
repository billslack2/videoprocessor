# VP-0028 proposed renderer configuration model

> **Proposal only — not implemented.** This document and its companion sample
> configuration define the design to review before changing
> `VideoProcessorRenderer.cfg`, its parser, or runtime behavior. Existing
> `[display_rules]`, `[shortcuts]`, and `[refresh_rate_commands]` remain the
> released configuration model until the migration described here is accepted.

A review-oriented HTML version is available beside the existing renderer help
as `VideoProcessorRenderer-Proposed.html`.

## Purpose

VideoProcessor uses the built-in libplacebo renderer for live streaming
sources. The configuration needs to select a small, predictable set of image
processing and display-calibration settings from trustworthy capture metadata.
It must also provide direct hotkeys and optional post-refresh actions without
giving those concepts unrelated special-case syntax.

The model has four independent profile groups:

1. **Input** chooses HDR/SDR processing from source metadata.
2. **Scaling** chooses scaling/deband policy from source size and cadence.
3. **Display** chooses the calibrated output contract and optional LUT.
4. **Viewport** chooses normal or scope geometry.

Each group has exactly one active profile. A profile changes only the settings
it owns. The effective renderer settings are built in this order:

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

## Terms

**Profile** is a named partial set of settings in one group.

**Automatic profile** has a `when=` expression. For a group in automatic mode,
the matching profile with the highest `priority` wins; equal priorities use
the most comparisons, then declaration order.

**Manual-only profile** has no `when=` expression. It is selected by the
group's configured default or by a binding. No impossible condition such as
`$width==0` is needed.

**Binding** maps one hotkey to one named selection action. Profile selection,
returning a group to automatic mode, and viewport selection all use the same
syntax and conflict rules.

**Event action** observes a completed renderer/display event. It may run a
configured command after a bounded delay. It cannot select a profile.

## Group selection

`[profile_groups]` declares the known groups and their startup selection. A
value of `auto` enables automatic selection for that group; a profile name
selects that profile manually and persists it in `VideoProcessorRenderer.state`.

```ini
[profile_groups]
groups=input,scaling,display,viewport
input=auto
scaling=auto
display=rec709_projector
viewport=normal
```

An automatic group with no matching profile contributes no group override;
`[display]` remains in effect for that group. A manual selection persists
across source changes until a binding chooses another profile or the group's
`.auto` action is invoked. Removed or invalid persisted profiles fall back to
the configured startup selection and emit one diagnostic.

## Profile syntax and ownership

Profiles use `[profiles.<group>.<name>]`. `profile_groups.groups` establishes
group order, so configuration never depends on file-section ordering.

```ini
[profiles.input.pq_hdr]
when=$transfer==PQ
priority=100
tone_mapping=spline
gamut_mapping=perceptual
peak_detection=high_quality
contrast_recovery=0.25

[profiles.display.rec709_projector]
sdr_target_primaries=REC709
sdr_target_nits=100
sdr_black_nits=0
output_range=FULL
output_gamma=AUTO
lut=lut\Projector-Rec709.cube
lut_reference_primaries=REC709
lut_reference_transfer=BT1886
lut_reference_range=FULL
lut_reference_nits=100
```

Only these settings are valid in each group:

| Group | Owned settings |
| --- | --- |
| `input` | `tone_mapping`, `gamut_mapping`, `peak_detection`, `contrast_recovery`, `sdr_input_transfer` |
| `scaling` | `quality`, `upscaler`, `downscaler`, proposed `sigmoid`, proposed named `deband_strength`, `dithering` |
| `display` | target primaries/nits/black, output presentation/range/gamma, display signaling, LUT and LUT reference contract |
| `viewport` | normal/scope selection, scope aspect, subtitle-fit settings and viewport geometry |

The base `[display]` section continues to supply defaults for every setting.
An unknown key or a key in the wrong group is a startup validation error; VP
must not silently place it in another group.

## Source conditions

Automatic `when=` expressions may use only stable source facts. Names and text
values are case-insensitive. Existing Boolean and comparison operators remain:
`==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`, and parentheses.

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

`$cadence` is a named exact rate, not a floating-point number and not a
truncated integer. `23.976` and `24000/1001` may be accepted as equivalent
spellings, but diagnostics must print one canonical value.

Source mastering data, MaxCLL, and MaxFALL are passed to libplacebo for color
mapping. They are deliberately not ordinary profile selectors: transient or
bad metadata must not rebuild the renderer repeatedly.

Do not use filename, title, codec, bitrate, container, or media-library data:
the streaming capture path may not have it, and it is not a reliable image
processing signal. Do not use output refresh in `when=`; it is the result of
profile selection and display switching.

## Bindings and hotkeys

`[bindings]` is the sole hotkey section. Every binding maps a selection target
to one key. The same parser accepts `F1` through `F24`, letters, numbers,
`Escape`, `Enter`, and optional `Ctrl`, `Alt`, and `Shift` modifiers.

```ini
[bindings]
display.rec709_projector=F5
display.bt2020_projector=F6
display.auto=Ctrl+F4
viewport.normal=F2
viewport.scope=F3
```

The target before the final dot is the group; the final token is either a
declared profile or `auto`. A hotkey always directly selects its target; it
never toggles. Duplicate bindings, unknown groups, and unknown profiles are
startup errors. The OSD/log must report the selected group, profile, and
whether it is automatic or manual.

This replaces the three released special cases:

| Released configuration | Proposed equivalent |
| --- | --- |
| `shortcut=F5` in `[display_rules.rec709]` | `display.rec709_projector=F5` |
| `screen_profile_scope=F3` in `[shortcuts]` | `viewport.scope=F3` |
| `display_rules_auto=F4` in `[shortcuts]` | `display.auto=F4` or `input.auto=F4` as intended |

## Refresh-transition event actions

Event actions are separate from profiles. They can observe only a completed
event, so a command never participates in renderer selection or runs per
frame.

```ini
[event_actions]
actions=audio_delay_24,audio_delay_60,restore_audio

[event_actions.audio_delay_24]
on=refresh.applied,refresh.confirmed
when=$actual_refresh==23.976|24
delay_seconds=5
command=C:\Videoprocessor\audio\audio_delay.bat 315

[event_actions.audio_delay_60]
on=refresh.applied,refresh.confirmed
when=$actual_refresh==50|59.94|60
delay_seconds=5
command=C:\Videoprocessor\audio\audio_delay.bat 285

[event_actions.restore_audio]
on=refresh.restored
when=$actual_refresh==60
delay_seconds=0
command=C:\Videoprocessor\audio\audio_delay.bat 0
```

Supported events are:

| Event | Meaning |
| --- | --- |
| `refresh.applied` | VP requested a refresh change and Windows reported the resulting current rate. |
| `refresh.confirmed` | VP found the requested refresh was already active; no mode change was made. |
| `refresh.restored` | Renderer shutdown/restoration successfully returned the display to VP's captured prior rate. |

There is no event for a failed or unsupported refresh request. Such failures
are logged and run no action. VP schedules at most one instance of each action
for one renderer generation and completed event. A pending action is cancelled
when its renderer generation is discarded, the renderer closes, or a newer
transition supersedes it.

`$actual_refresh` is the actual rate read after an applied change or restoration,
or the already-active rate for `refresh.confirmed`. `$requested_refresh` and
`$previous_refresh` are diagnostic event facts and may be added later; they are
not required for the initial syntax. A command is trusted local configuration:
VP logs the action name, event, rate, delay, and result, but should avoid
duplicating sensitive command content in normal diagnostics.

## Streaming policy

The initial profile library should remain small:

```text
input:   sdr | pq_hdr | hlg_hdr | lldv_like
scaling: sd_hd | mid | uhd_native
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

## Migration and review boundary

The runtime implementation must accept the released sections for a defined
compatibility period:

- `[display_rules]` and `[display_rules.name]` map to the appropriate proposed
  profile group only when their keys have one clear owner.
- `shortcut=` maps to an equivalent `[bindings]` entry.
- `[shortcuts]` maps to viewport or `.auto` bindings.
- `[refresh_rate_commands]`, including its legacy `command=` form, maps to
  refresh event actions with the documented old truncated-rate behavior.

Legacy configuration is never rewritten automatically. VP emits one actionable
deprecation diagnostic per legacy construct, preserves existing behavior during
the compatibility period, and provides this sample as the migration target.

Before code implementation, review this proposal together with
`docs/examples/VideoProcessorRenderer.unified.proposed.cfg`. The documentation
is accepted first; only then should parser tests and runtime changes begin.
