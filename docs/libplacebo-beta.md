# Optional master-based VP Renderer

## Renderer choices and fork branches

- **VP Renderer**: shipped libplacebo 7.360.1 DLL and headers, unchanged.
  Complete source is now published at
  [codex/vp-release-7.360.1](https://github.com/billslack2/libplacebo/tree/codex/vp-release-7.360.1),
  commit `192b4b4ea634ed65bb93abe9a3f4e6339d5280e3`.
  This reconstructs the original bundled source archive, including the timer
  allocation/suspension patches absent from the previously published branch.
- **VP Renderer (Beta)**: upstream master pinned to
  `1dd022687b9ac1dd996855b51433bf770ae76aab`, with VP analysis crop,
  master scene-statistics crop support, timer controls, dynamic-target toggle,
  and opt-in VP smooth highlight color mapping.
  [codex/vp-master-beta](https://github.com/billslack2/libplacebo/tree/codex/vp-master-beta),
  commit `ed8e9a6c9d6603e3a272503b94f9946535301072`, API **377**.

Both branches belong to `billslack2/libplacebo`. Upstream updates should be
explicitly pinned and tested on the Beta branch. Do not automatically rebuild
VP against whatever upstream master happens to contain.

## Config

Select the renderer in **General > Display > Renderer**. Existing renderer
aliases continue selecting stable. The new controls use the real native Config
UI under **VP Renderer > Rendering > Tone Mapping: Highlight Response (Beta)**.

| Rendering profile key | Default | Range | Effect |
|---|---|---|---|
| `custom_tone_mapping` | `false` | Boolean | Off uses original master mapping; On enables experimental VP smooth highlight color compression during HDR compression. |
| `dynamic_target` | `true` | Boolean | Adapt the virtual target to scene brightness, trading average brightness for highlight headroom. |
| `film_strength` | `AUTO` = 0.3 | 0–2 | Blend toward the LMS film response; 0 preserves source chromaticity. Above 1 may clip highlights. |
| `mastering_clip` | `AUTO` = 1 | 0–1 | Soften channel excursions above trustworthy mastering-peak metadata. 0 disables this step. |

The **Custom highlight color mapping** checkbox defaults Off. Select Beta,
change this checkbox, then Apply to compare using the same tone curve and
physical target. The original Gamut mapping selector is disabled while this
option is enabled; its saved choice is restored when the option is turned off.
The custom path activates during HDR compression; SDR passthrough and inverse
tone mapping retain the original mapping.

These fields belong in `[vprenderer.<profile>]`. Missing keys inherit; `AUTO`
explicitly resets the two numeric fields to library defaults. Dynamic target
is a checkbox, default On. Turning it off leaves peak detection and film
response independent. Scene-average metadata is needed for adaptation;
measurement supplies it when available. **Target nits remains the physical
display target**, regardless of this toggle.

Stable disables the Beta controls and ignores their runtime values. Switching
renderers preserves saved values. The existing spline/knee/peak controls remain
applicable to master and retain their existing defaults.

```ini
[general]
renderer: VP Renderer (Beta)

[vprenderer.Default]
custom_tone_mapping: false
dynamic_target: true
film_strength: AUTO
mastering_clip: AUTO
```

## Binary and build isolation

Stable uses `vprenderer/VideoProcessorVPRenderer.dll` with `libplacebo-360.dll`.
Beta uses `vprenderer-beta/VideoProcessorVPRendererBeta.dll` with
`libplacebo-377.dll`. Headers, import libraries, object directories, exported
parameter reads and persistent shader caches are separate. Plugin API 25 also
checks a renderer-flavor export, rejecting a plugin placed under the wrong name.
All eight shared transitive runtime DLLs are currently byte-identical. This is
required because Windows can reuse loaded DLLs by basename across directories.

The Release solution builds both plugins. Beta uses the same renderer source,
compiled with `VP_LIBPLACEBO_BETA`. The standalone parameter smoke test is:

```powershell
MSBuild src/VideoProcessor-BetaTests/VideoProcessor-BetaTests.vcxproj /p:Configuration=Release /p:Platform=x64
./x64/Release/vprenderer-beta/VideoProcessorBetaTests.exe
python tools/test_libplacebo_variants.py
python tools/test_libplacebo_variants.py --beta-first
```

Full fork source, dependency pins, build instructions, DLL/source hashes and
licenses are bundled under `3rdparty/libplacebo-beta`. See its README and the
source archive's `VP-FORK.md`. Build libplacebo with **meson compile**, then run
**meson test**; tests alone may only relink the static test dependency, leaving
the shared DLL's version stale. Confirm `pl_version()` before bundling.

The existing SDR swapchain hint workaround is retained. The master-specific
transfer-hint change does not justify removing this compatibility policy.

## Validation (2026-10-11)

- x64 Release solution: success, both renderer plugins and native Config.
- Master fork: all 13 CPU/D3D11 tests pass. Includes real output on/off/on
  dynamic-target comparison, cropped maxRGB statistics and timer controls. The
  custom mapping A/B test also covers feature extraction/contrast recovery and
  exact restoration of the original output with reused shader state.
- VP focused native parameter/plugin tests: 29 passed after the custom-toggle
  change, including stable isolation and Beta DLL naming. The preceding feature
  baseline also passed its 83-test renderer/output-policy selection.
- Native Qt tests: Beta field gating, inheritance, save/reload, defaults and
  validation; existing tone-control round trip passes.
- Beta projection smoke test passes for all quality presets, overrides,
  independent peak detection and SDR/HDR luminance invariants.
- Both plugins load simultaneously in both orders with the correct libplacebo
  DLL and flavor. Master reports the clean `ed8e9a6c` source revision.
- Release copy-plan validation includes both plugins and runtime records.
- Installer support: 59 checks; installer identity: 12 checks; portable config
  contract: 8 checks passed.

Live capture/playback, physical HDMI output and clean Windows installer lifecycle
qualification are still required before release. No deployment or installer
installation was performed for this feature branch.

## BT.2020 gamut review

### Reproduction

`tools/libplacebo_gamut_probe.c` renders floating-point linear BT.2020 output
through the fork's actual D3D11 shader pipeline. It uses saturated hue ramps
with a **4,000-nit maximum** and **456.761-nit mean maxRGB** (the mean luminance
is lower), three display targets, four tone curves, four gamut mappers,
film strengths 0/0.3/1/2, and dynamic target off/on. These are synthetic scene
statistics, not a claim to reproduce a particular film frame.

The output is measured before integer conversion or transfer-function clipping.
Raw measurements are in [libplacebo-beta-gamut-probe.csv](libplacebo-beta-gamut-probe.csv).
For a 100-nit target, default film strength 0.3 and dynamic target On:

| Tone curve | Minimum channel (nits) | Maximum channel (nits) | Pixels outside bounds / 3072 |
|---|---:|---:|---:|
| spline | -0.963 | 117.570 | 166 |
| bt2390 | -0.551 | 119.487 | 415 |
| st2094-40 | -1.228 | 127.960 | 277 |
| reinhard | -0.821 | 116.220 | 399 |

Perceptual, softclip, relative and desaturate produce the same measurements in
this equal-primaries case. Dynamic target On does not solve containment.
Film strength 0 largely removes the excursions in this probe; small numerical
overshoots remain (for example roughly 0.07 nit at a 500-nit target).

### Cause and proposed improvement

In the pinned upstream source,
[`pl_gamut_map_params_noop`](https://github.com/haasn/libplacebo/blob/1dd022687b9ac1dd996855b51433bf770ae76aab/src/gamut_mapping.c)
can skip gamut mapping when input and output primaries match.
[`pl_shader_color_map_ex`](https://github.com/haasn/libplacebo/blob/1dd022687b9ac1dd996855b51433bf770ae76aab/src/shaders/colorspace.c)
then applies the film response in LMS, which can create negative or excessive
RGB components. The later output clamp/quantization can hide the excursion
by clipping. Merely choosing a different gamut mapper cannot address a stage
that was skipped. Nonzero film response also deliberately changes chromaticity;
this probe does not establish whether every perceived oversaturation issue
has the same cause.

### Implemented opt-in comparison

The earlier proposal to blend toward the film-strength-zero result was rejected
by further ramp testing: it could increase chroma and create brightness reversals.
The implemented option uses a smooth, hue-aware color-volume shoulder instead.
It replaces the original gamut stage during HDR compression and runs after the
existing tone/film/contrast-recovery response and final conversion to output RGB.

- Uses the physical output primaries, black and peak; the dynamic virtual target
  does not change these bounds. Tone curve, film response and dynamic target
  remain independently selectable.
- Finds the first boundary from neutral at fixed CIE Y and IPT hue, then gradually
  compresses chroma near that boundary. Luminance rolls off near the display peak.
  Interior midtones are protected; independent channel clipping is not the
  mapping operation.
- Uses a 129 x 33 x 513 float RGBA table (about 33.3 MiB) and four filtered
  samples per pixel. The table is reused in shader state while physical target
  primaries/black/peak remain unchanged. It is not stored in the persistent disk
  cache. Changing the physical target requires regeneration.
- LUT nodes are bounded; interpolation between bounded RGB values remains
  bounded. Explicit interpolation in Y avoids hardware interpolation precision
  producing small brightness reversals. Unsupported float32 filtering or table
  generation failure rejects the shader with a diagnostic.
- Default Off follows the original fork/master code path. The release fork and
  release renderer ignore the custom option.

### Integrated GPU measurements

`tools/libplacebo_ab_probe.c` renders original/custom/original with one reused
shader state. It checks bit-exact restoration on every case and writes float
output before encoding or integer clipping. It covers 320 cases / 2,990,080
pixels: equal BT.2020, P3-D65 mastering inside BT.2020, and BT.2020 to BT.709;
100/203/500-nit targets, four curves, film strength 0/0.3/1/2 and dynamic target
Off/On, plus detected-metadata/mastering-clip cases. The synthetic source peak
is 4,000 nits, with mean maxRGB 456.761 nits in the equal-BT.2020 case.

Final DLL `ed8e9a6c` results:

- All 320 cases restored the original output exactly after toggling Off.
  Original output also matched the previous API 376 fork bit-for-bit.
- All custom pixels were finite and within physical RGB bounds at a 0.001-nit
  measurement tolerance. Alpha was unchanged.
- The 32 default-film-strength equal-BT.2020 cases had no new measured brightness
  reversals or IPT chroma increases on material overflow pixels. Maximum IPT hue
  change on those chromatic overflow pixels was 0.916 degrees.
- Across all film strengths, equal-BT.2020 had 22 small measured chroma increases
  and a maximum hue change of 1.674 degrees. P3 and BT.709 cases showed larger
  differences from the original gamut mapper (up to 25.84/32.95 degrees), and
  7/3 newly measured brightness reversals. The original gamut stage and the VP
  replacement have different color behavior in these cases; this comparison
  does **not** establish universal hue preservation or superiority.

See [libplacebo-beta-ab-comparison.csv](libplacebo-beta-ab-comparison.csv) for
per-case metrics. Hue/chroma metrics use pixels whose original RGB exceeds the
physical box by more than max(0.02 nit, 0.02% of target); hue excludes chroma below
0.001. A brightness reversal is a ramp step below -max(0.001 nit, 0.001% of target)
that was not already present in original output. Chroma increase threshold is
0.00001 IPT. These are measurement thresholds, not perceptual guarantees.

This remains an experimental viewing option. Real problem scenes, temporal
stability and sustained 4K GPU performance need qualification. No containment
claim applies to calibration/custom LUTs or other processing after this stage.
The internal 16x16 fork regression includes contrast recovery; the 320-case
numerical sweep does not include a feature map. This is not an ACES transform.

Build the comparison probe against the matching Beta import library and run it
from an empty output directory (the four output files are overwritten):

```sh
gcc /path/to/vp/tools/libplacebo_ab_probe.c \
  -I /path/to/vp/3rdparty/libplacebo-beta/include \
  -L /path/to/vp/3rdparty/libplacebo-beta/lib -l:libplacebo-377.lib -lm -o ab-probe.exe
PATH=/path/to/vp/3rdparty/libplacebo-beta/bin:$PATH ./ab-probe.exe
python /path/to/vp/tools/analyze_libplacebo_ab.py .
```

The analysis requires NumPy. Keep the final line identifying the library version
from stderr alongside the measurements.

To reproduce the probe from an MSYS2 mingw64 shell, use the matching fork build:

```sh
gcc tools/libplacebo_gamut_probe.c -I 3rdparty/libplacebo-beta/include \
  -L /path/to/vp-master-beta/build-release/src -lplacebo -lm -o gamut-probe.exe
PATH=/path/to/vp-master-beta/build-release/src:$PATH ./gamut-probe.exe > gamut-probe.csv
```
