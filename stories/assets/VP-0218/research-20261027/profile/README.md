# VP-0218 boundary-led profile prototype

This is diagnostic-only research against the actual captured Limited-range V210 data. It does not change VP source, grant crop authority, or claim held-out real-video validation. Inputs and predeclared thresholds are in `protocol.txt`; executable code is `boundary_profile_experiment.py`; all full numeric outputs are in `boundary-profile-results.json`.

Reproduce with:

```powershell
& 'C:\Program Files\Python310\python.exe' 'E:\codex\diagnostics\vp-0218-boundary-research-20261027\profile\boundary_profile_experiment.py'
```

The detector scans the upper and lower half independently from their respective physical raster edges. It uses 96 distributed horizontal probes; no CIH, screen target, aspect ratio, symmetry, or hard-coded ad coordinate nominates either edge. After each first supported Y/chroma transition, it verifies the line against every source column within a four-row neighborhood. An eight-row uncertainty fringe is kept on both edges.

The native capture has 97 saved payload identities but only four unique payload hashes (24, 24, 24, and 25 repeats). All four independently locate the upper transition outside row 300 / inside row 301 and the lower transition inside row 1856 / outside row 1857 (see the saved JSON label correction below). Quartile support is 100% for both; full-column line verification is 100%, with P95 row error 2 pixels at the top and 0 at the bottom. The fixed fringe produces candidate coordinates top=293, bottom-exclusive=1865. The nominal measured transition and guarded coordinate are separate values.

The previous fixed 16-cell linear-depth fit rejected the upper exterior (held-out Y residual P95 17.4, maximum 90.2, with large connected residuals). The frozen local-background prototype changes the failure mode: the upper exterior is neutral (fraction 1.0), local Y residual P95/P99 are 0/4, chroma residual P99 is 2/3, and 32-pixel tile-median step P95/max are 8/18. Those checks pass the predeclared filters. Its connected-feature veto still rejects the ad: 22 components of at least 16 pixels, largest 136 pixels. The independent lower exterior passes every profile check. This is evidence that the coarse linear model was too restrictive for describing the upper background, but it does not make the current candidate safe to crop.

Synthetic controls exercise the chosen gates:

| Case | Prototype result | What it establishes |
| --- | --- | --- |
| Neutral spatial gradient matte | Pass | Local background tolerates the tested low-frequency gradient. |
| Pixel-identical “authored ceiling” label | Pass | Semantic labels cannot distinguish identical pixels; this is an identifiability control, not an operational negative by itself. |
| Fine bright caption in exterior | Reject | Connected high residual catches this generated caption (largest component 2,425 pixels). |
| Colored exterior object | Reject | Neutral-chroma and residual checks reject the generated object. |
| Broad smooth neutral +8-code object | Pass | A real blind spot: smooth, low-contrast structure can be absorbed by the local background. |
| Neutral gradient with Gaussian noise sigma 4 | Reject | The fixed local Y residual percentile limit rejects this noisy control. |

The tests are deliberately not tuned after results. The prototype is small enough to explain, but the Python implementation’s 17x17 median filter is not a production performance result. A production implementation would need a fixed sample/work budget and profiling within the existing analysis budget. More importantly, its connected-veto false rejection and broad-object false acceptance show that this rule set is not qualified. The next discriminating work is to inspect the 22 captured upper residual components and compare their size/shape/temporal behavior against the generated caption, sparse-star, logo, overlay, and real held-out matte/artwork controls without lowering safety gates to force the ad through. The current four unique hashes are a short repeated clip, not an independent positive corpus.

## Component follow-up (frozen verdict unchanged)

`component_shape_analysis.py` recomputes the same 17x17 luma median residual and the frozen `Y residual >=12`, four-connected, area>=16 rule. It only adds descriptive shape/bounds/peak statistics; it does not change component thresholds or the route result. `component-shape-analysis.json` is the output.

On the first unique ad payload, the 22 vetoing components have area min/median/P95/max 19/51/97.5/136 pixels. Their bounding-box widths are 4–19 pixels and heights 1–16 pixels; one-pixel high-pass strips can span width19. Peak residuals range 12–160 codes (median24). The four unique payloads have the same 22 component boxes exactly, though residual magnitudes can differ. This repeatability suggests stable image texture or fixed image detail; it does not tell us whether viewers would treat it as expendable.

The generated caption yields two much larger components, 2,425 and 60 pixels. The largest spans 240x24 pixels and peaks at 745 residual codes. The generated sigma-4 noise and broad +8 smooth object have no component meeting the area16 gate. These measured differences suggest that component shape/scale and edge-connected glyph structure may discriminate this specific generated caption from the captured small residual texture, while a simple area16 rule is overly blunt. They do not validate a threshold for real subtitles, logos, sparse stars, or encoded grain. Existing subtitle/overlay ownership and visibility vetoes must remain authoritative.

### Errata

The protocol wording “original first four native capture payloads” was imprecise: the script hashed the full 97-row manifest and selected the first payload for each of the four unique SHA-256 values. This still yields files 1–4 and repeat counts 24/24/24/25. In `boundary-profile-results.json`, for the bottom scan the field `transition_outside_row` contains the scan pair's inside row `r=1856`; the actual adjacent outside row is `r+1=1857`. The nominal bottom edge is inside=1856/outside=1857. The edge detection, line support, exterior crop coordinate, and pass/fail results do not change.

