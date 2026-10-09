# Subtitle profiles

**VP Renderer → Subtitles** contains standard ordered profiles with shortcuts, shared cycle shortcuts, source rules, persistence, live selection, and top OSD integration. The first profile is the default.

## Processing styles

| UI style | Stored type | Behavior |
| --- | --- | --- |
| Off | `off` | No subtitle-driven placement; normal Screen/Zoom rules apply. |
| Classic | `classic` | Original picture fitting, with its timing and padding controls. |
| Move / Reconstruction (Beta) | `generated_gray` | Moves subtitles over reconstructed picture with a configurable background. |
| Move / Solid Background (Beta) | `black` | Moves subtitles onto a fully opaque configurable color. |

Stored type names and appearance key names remain compatible with existing subtitle profiles. Renaming a profile changes its name in the OSD independently of its processing style.

## Placement and Classic controls

`offset_pixels` is the common inward placement setting (0–500 source pixels). Bottom subtitles move higher; top subtitles move lower. In Classic this is the original subtitle padding, so there is no duplicate padding control. Off ignores placement.

Classic also exposes:

| Setting | Range | Default |
| --- | --- | --- |
| `subtitle_hold_seconds` | 0.25–30 seconds | 2 |
| `subtitle_engage_drift_ms` | 0–30000 ms | 500 |
| `subtitle_release_drift_ms` | 0–30000 ms | 2000 |
| `subtitle_target_buffer_pixels` | 0–50 source pixels | 10 |

Selecting Off replaces the old subtitle-fit checkbox.

## HDR analysis protection

HDR analysis protection is configured in **Subtitles**, common to all processing styles, and is no longer shown on Zoom. Off uses normal analysis. Smart excludes the edge occupied by Classic picture movement or by a successfully moved subtitle overlay and its original backing. Percentage selects a 10-100% picture-height band with top, center, or bottom alignment.

The existing keys `hdr_peak_analysis_picture_only`, `hdr_peak_analysis_motion_compensation`, `hdr_peak_analysis_height_percent`, and `hdr_peak_analysis_position` are accepted in subtitle profiles. Explicit subtitle values override legacy Zoom values. Existing saved Zoom settings remain a compatibility fallback until edited; opening the controls does not migrate or rewrite them. Percentage takes precedence if both legacy booleans are enabled.

## Moved subtitle appearance

Both moved styles share background color, border color, border opacity (0–1), and border width (0–8 source pixels; 0 disables). Colors have a preview and editable six-digit sRGB hex fields. Choose color expands a compact inline picker with basic color swatches, a hue/saturation surface, and a brightness control. Custom-color storage and duplicate numeric/hex controls are omitted; the external RGB field remains available. No popup or modal window is opened. The picker and hex field stay synchronized when switching profiles; opacity remains a separate setting.

Reconstructed additionally exposes background opacity (0–1), whole-overlay blur (0–30 source pixels), and brightness limit (0.01–1 in linear reference-white units). Solid uses the selected color directly and remains opaque; reconstruction opacity, blur, and brightness compression do not affect it. Unset Solid color is black. Unset Reconstructed color is 040404, opacity 0.85, blur 3, and brightness limit 0.16.

Existing `subtitle_generated_gray_*` appearance keys are retained inside subtitle profiles for file compatibility, including when used for Solid background. Switching styles hides irrelevant controls without deleting their saved values. Defaults and inheritance follow the standard profile system.

`subtitle_hold_ms` (0–1000; default 250) and `subtitle_near_bar_px` (0–200; default 20) remain file-only controls for moved subtitles. Detector optimizations remain disabled. Detection, stabilization, extraction, and picture cleanup algorithms are unchanged by this UI update.

## Example

```ini
[vprenderer.subtitles.Classic]
type: classic
offset_pixels: 20
subtitle_hold_seconds: 2
subtitle_engage_drift_ms: 500
subtitle_release_drift_ms: 2000
subtitle_target_buffer_pixels: 10
cycle_shortcut: Ctrl+Shift+T

[vprenderer.subtitles.Reconstructed]
type: generated_gray
offset_pixels: 15
cycle_shortcut: Ctrl+Shift+T
subtitle_generated_gray_color: 040404
subtitle_generated_gray_opacity: 0.85
subtitle_generated_gray_blur_px: 30
subtitle_generated_gray_max_luminance: 0.16
subtitle_generated_gray_border_color: 000000
subtitle_generated_gray_border_opacity: 0.65
subtitle_generated_gray_border_width: 0

[vprenderer.subtitles.Solid]
type: black
offset_pixels: 15
cycle_shortcut: Ctrl+Shift+T
subtitle_generated_gray_color: 000000

[vprenderer.subtitles.Off]
type: off
cycle_shortcut: Ctrl+Shift+T
```

Selection uses standard profile persistence. Legacy subtitle settings in Rendering, Screen, and Zoom remain ignored and preserved on disk. The retired `shortcuts.subtitle_toggle` binding is replaced by profile shortcuts.

## Offset backgrounds and live cycling

For Reconstructed and Solid background, the panel extends from the offset text to the adjacent physical letterbox boundary. Bottom panels extend down to the lower bar; top panels extend up to the upper bar. The text keeps its configured offset. The extension is background-only: it does not expand glyph capture, source ownership, or aspect-ratio detection. Reconstruction blur covers the entire extended panel, and bar-facing corners stay square.

Subtitle settings participate in the effective-settings fingerprint, but not the device/transport rebuild fingerprint. Profile cycling and subtitle appearance changes use the existing render-thread live-update queue without recreating the renderer or flushing the frame queue. Output/device/cache-policy changes still retain their normal restart requirements.

### Capture edge follow-up

Capture keeps a two-native-pixel horizontal antialias guard inside the accepted
cue envelope. Vertical line masks remain tight: expanding them can copy bright
picture seams between lines or just below the lettering. Display padding is
independent. Picture-side glyphs still require measured opaque-card ownership.

After accepting a cue, a bounded raster-component pass can attach substantial
same-baseline marks within 15 source pixels, up to four hops and 60 pixels total.
It reuses the existing components, requires a dark halo, rejects tiny noise chains
in the unbounded physical bar, and stays in the physical bar or measured card.
Small same-baseline marks inside the measured card interior do not need letter
height. The outside cleanup fringe does not grant that exception. Existing specialized punctuation
and diacritic grouping remains in place. It does not create new caption eligibility.

Measured straight card sides may expand to a nearby corroborated wider edge
(with at least half the modal support, limited to half a glyph height). Cleanup
adds two native pixels beyond measured sides and vertical edges, clipped to the
picture. An occluded, inferred side does not receive extra horizontal authority.

Validation uses synthetic native/GPU cases for capture, cleanup, offset fill,
seam rejection, dark scenery, temporal tracking, and aspect detection. The supplied
2026-10-08 recording was inspected at the professor-question and Rebellion cues;
processed/paused frames were not treated as raw detector input. Live HDMI replay
and shortcut cycling still require a user run of the resulting Release build.

### Classic defaults restored

The independent Classic profile defaults to a 2-second hold, 500-ms engage drift,
2000-ms release drift, 20-source-pixel picture padding, and a 10-source-pixel
target buffer. These restore the smooth transition timings used by the earlier
configured Zoom profiles. Missing values use these defaults; explicit values,
including zero drift or zero padding, remain unchanged. Old Zoom subtitle values
remain ignored. Moved subtitle inset, glyph headroom, and weak-detection hold are
separate controls and are not assigned these Classic defaults.


### October 8 refinement validation

The editor uses an embedded Qt color dialog for full-range RGB selection, alongside
hex entry; it does not open a modal window. Moved profiles expose
`minimum_headroom_pixels` (0�200 source pixels, default 12). This is a minimum
above the frozen glyph capture envelope, independent of detection and cleanup.

Raw frames at 8 and 24 seconds from `2026-10-08 10-51-23.mp4` were decoded at their
recorded 2560�1440 resolution and passed through the detector and GPU shader with
known picture edges. Both static outputs retain the question mark, and the
reported thin interior seam did not reproduce. This diagnostic does not replay
live 3840�2160 acquisition, aspect detection, or temporal hold, and therefore does
not establish that those live failures are resolved.


### Optional moved text size reduction (Beta)

Reconstructed and Solid background profiles expose **Text size reduction** as an
integer percentage from 0 to 75, stored as `text_reduction_percent`. The default
is 0: existing subtitle text size is unchanged. A value of 75 renders the text at
25% of its original width and height. This control is hidden for Classic and Off;
switching styles preserves its saved value. It changes moved text rendering, not
the source caption detection or cleanup area.

### Text reduction rendering and qualification

Reduction applies uniformly to the complete moved glyph block without OCR
redrawing or reflow. Source capture, per-line ownership, original-card cleanup,
detection and AR authority remain unchanged. The stable source envelope sets the
scale pivot; ordinary glyph-bound corrections cannot make the reduced text breathe.
Inward offset, padding and minimum headroom remain absolute source-pixel values.

The display panel may shrink around the reduced text, but it is unioned with the
original cleanup area inside the visible picture. Original intrusions therefore
remain fully covered for both Solid and Reconstruction, including offsets and
top captions. Scaling is applied after physical viewport fitting, and only the
display panel is clipped to that viewport; extraction authority is not enlarged.

The GPU keys each native glyph sample before area filtering. This avoids losing
thin strokes and isolated punctuation when 4x4 source footprints become one
output pixel at 75% reduction. Filtering is bounded to the owned glyph footprint
and adds no GPU pass/readback or detection latency. Zero reduction retains the
original sampling path. Profile changes use dynamic parameters without requiring
a renderer restart. Actual live GPU overhead should be checked with the existing
subtitle timing telemetry when using the option.

Qualification: 357/357 targeted subtitle tests, 2314/2314 full native tests,
and three subtitle UI save/reopen/style tests passed in x64 Release. Independent
geometry review passed 2,700 combinations of reductions, edges, offsets, padding
and viewport crops. GPU regressions cover source ownership, original-card
coverage, source removal, area-weighted glyphs and one-pixel punctuation phases.
Five existing-recording renders at 0/25/50/75% reduction and a two-line 75% example
were visually inspected. The 0% recording render is byte-identical to the prior
verified miss-fix build. These resized recording proxies do not establish native
HDMI playback performance. `subtitle-scaling-verification.json` pins the exact
Release build and evidence. This feature has not been deployed.


## Centered moved subtitle boxes and background edge

Both beta moved styles accept `rounded_corners: true` (default) and
`background_edge: extend` (default) or `float`. The UI exposes these under
Moved subtitle appearance. Classic and Off ignore them.

Text is centered inside its padded cue box at every reduction. Offset positions
the whole box away from the corresponding picture edge; the backing extension
is excluded from the centering calculation. Symmetric padding honors the larger
top/bottom minimum when space permits, reducing only impossible margins.
Float keeps all four corners visible; Extend squares the bar-facing corners.
Verified source-picture overlap forces extension for the remainder of that cue.
Display coverage is retained independently from current capture/cleanup proof.
Settings apply live and do not modify detection or aspect-ratio authority.
