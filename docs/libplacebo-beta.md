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
  master scene-statistics crop support, timer controls and dynamic-target toggle.
  [codex/vp-master-beta](https://github.com/billslack2/libplacebo/tree/codex/vp-master-beta),
  commit `7091553e68bed28e5a39a82af298ca681511fd5a`, API **376**.

Both branches belong to `billslack2/libplacebo`. Upstream updates should be
explicitly pinned and tested on the Beta branch. Do not automatically rebuild
VP against whatever upstream master happens to contain.

## Config

Select the renderer in **General > Display > Renderer**. Existing renderer
aliases continue selecting stable. The new controls use the real native Config
UI under **VP Renderer > Rendering > Tone Mapping: Highlight Response (Beta)**.

| Rendering profile key | Default | Range | Effect |
|---|---|---|---|
| `dynamic_target` | `true` | Boolean | Adapt the virtual target to scene brightness, trading average brightness for highlight headroom. |
| `film_strength` | `AUTO` = 0.3 | 0–2 | Blend toward the LMS film response; 0 preserves source chromaticity. Above 1 may clip highlights. |
| `mastering_clip` | `AUTO` = 1 | 0–1 | Soften channel excursions above trustworthy mastering-peak metadata. 0 disables this step. |

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
dynamic_target: true
film_strength: AUTO
mastering_clip: AUTO
```

## Binary and build isolation

Stable uses `vprenderer/VideoProcessorVPRenderer.dll` with `libplacebo-360.dll`.
Beta uses `vprenderer-beta/VideoProcessorVPRendererBeta.dll` with
`libplacebo-376.dll`. Headers, import libraries, object directories, exported
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
  dynamic-target comparison, cropped maxRGB statistics and timer controls.
- VP focused native tests: 83 passed, covering renderer identity, stable
  isolation, render parameters, output policy and private runtime paths.
- Native Qt tests: Beta field gating, inheritance, save/reload, defaults and
  validation; existing tone-control round trip passes.
- Beta projection smoke test passes for all quality presets, overrides,
  independent peak detection and SDR/HDR luminance invariants.
- Both plugins load simultaneously in both orders with the correct libplacebo
  DLL and flavor. Master reports the clean `7091553e` source revision.
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

**There is a clear path for a bounded experiment in our master fork:**

1. Retain the existing chromaticity-preserving endpoint (film strength 0) and
   the requested film-response endpoint in the actual destination linear RGB
   space, after exposure and black adaptation.
2. For pixels outside the destination volume, reduce the film contribution to
   the largest blend whose three channels lie inside the physical output bounds.
   A smooth shoulder near that bound can avoid an abrupt change of response.
   Leave already-contained pixels alone. Handle numerical roundoff separately.
3. Apply containment after every stage that can reintroduce an excursion,
   including contrast recovery; test mastering P3 inside a BT.2020 container,
   equal BT.2020, narrower gamuts, authored/detected metadata and LUT output.
4. Require finite/legal output, monotonic highlight ramps, neutral preservation,
   limited hue error, no unintended chroma increase and temporal stability.
   Validate the user's actual problem scenes before selecting defaults.

This approach can retain useful film highlight whitening while avoiding a
channel clamp as the main containment strategy. It is a proposed improvement,
**not an implemented gamut fix**. Forcing the existing equal-gamut 3D LUT on
alone is insufficient: LUT domain limits and numerical containment also matter.

To reproduce the probe from an MSYS2 mingw64 shell, use the matching fork build:

```sh
gcc tools/libplacebo_gamut_probe.c -I 3rdparty/libplacebo-beta/include \
  -L /path/to/vp-master-beta/build-release/src -lplacebo -lm -o gamut-probe.exe
PATH=/path/to/vp-master-beta/build-release/src:$PATH ./gamut-probe.exe > gamut-probe.csv
```
