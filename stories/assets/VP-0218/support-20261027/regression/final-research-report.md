# VP-0218 boundary-support regression research — 2026-10-27

Scope

Read-only diagnostics under this directory. The fresh beta source worktree stayed at `7394ba42` and clean. No production source, configuration, running process, deployment, tests, or thresholds were changed. The native probe linked existing 7394-beta x64 Release objects from `E:\codex\diagnostics\vp-0218-boundary-research-20261027\baseline`.

Real native captures and decoder checks

The capture binaries were fed through unchanged `AnalysisLumaSource::Sample` at every coordinate, then compared sample-for-sample against independent Python P210/V210 unpackers. Each result matched exactly across all 8,294,400 pixels in each Y/U/V plane. Sidecars have ten fields and no range field; `AnalysisLumaRange::Unknown` was preserved.

| Capture | Source format | Beta measured bounds/class | Raw black floor/threshold; IsCrediblyVisible Y threshold; global nearblack cutoff | Retention on measured bounds |
| --- | --- | --- | --- | --- |
| City-lights | Decoded DolbyVision422 / R210 | `(0,66)-(3840,2092)`, `BAR_CROP_TRUSTED` | `0/24`; `32`; `96` | pixel-safe |
| Thai top-card | NativeYuv422 / V210 | `(0,274)-(3840,1884)`, `BAR_CROP_TRUSTED` | `64/88`; `96`; `96` | veto; outward top `251` |
| Latin three-lines | NativeYuv422 / V210 | `(0,274)-(3840,1884)`, `BAR_CROP_TRUSTED` | `64/88`; `96`; `96` | veto; outward bottom `1933` |

The sidecar top/bottom values were not used as labels. The two caption bounds above come from unchanged beta extraction and are only candidate geometry. Grayscale previews show the Thai text at the upper picture edge and Latin captions at the lower edge; those outward measurements are caption/content envelopes, not active-aspect labels. City-lights contains no visible caption in the preview and its excluded bands have zero visible samples; this agent did not rerun the subtitle detector.

On the frozen morphology veto, city-lights has no vetoing exterior component. The Thai top exterior (guarded candidate `[0,266)`) has two 8-connected thresholded components, one morphology veto and one dense-bin veto. The Latin bottom exterior (`[1892,2160)`) has seven components, one morphology veto and two dense-bin vetoes. Both real caption examples remain vetoed by unchanged beta retention as well.

The production-shaped 288-point summary passes on the Thai and Latin caption bands in both the raw and median-17 residual forms; it does not describe the caption. Actual raw thresholds were verified from the beta `blackReference` fields: city-lights uses legacy floor/threshold `0/24`, Thai/Latin `64/88`. `IsCrediblyVisible` uses `black_threshold + 8` (32 for city-lights; 96 for Thai/Latin). The separate `global_cutoff` field is the global nearblack cutoff, 96 in all three captures; it is not the visibility threshold. The corrected city-lights run still finds zero raw, reference-relative, or intersection visible samples.

Sampling phase matters. On nominal beta bands, Thai top raw visibility is 10 samples / 2 adjacent occupied rows, reference-relative visibility is 8 / 2 adjacent rows, and their intersection is 8 / 2. On the 8-row-guarded band all three fall to 3 samples and no row with two samples. Latin bottom nominal gives raw 30 / 6 rows with adjacent support, reference 12 / 3 rows without adjacency, and intersection 12 / 3 without adjacency; guarded gives 18 / 3 raw rows with adjacency, reference/intersection 8 / 2 without adjacency. Guarding and normalization can both weaken these sparse witnesses. The broader beta retention scan still catches both captions.

Held-out synthetic controls and unchanged beta

Ten full-resolution 3840x2160 controls were frozen in `synthetic/heldout-protocol.txt` and generated before classifier evaluation. They use a correct P210 serialization (full Y plane then interleaved UV plane). Python checked every code in all three planes after serialization; unchanged C++ `Sample` then matched Y/U/V at all 8,294,400 pixel coordinates for every control. `beta-results.csv` records raw thresholds and retention. The beta detector returned `PROVISIONAL` and rejected the supplied `(293,1865)` inset presentation for all ten controls.

The frozen morphology and fixed 12x6 spline measurements are separate from the reference-relative visibility measurements. The spline aggregate is a model-fit diagnostic; no end-to-end spline-plus-visibility crop route was run.

| Control | Frozen morphology top | Spline held-out abs P90/P99/max | Beta outcome |
| --- | --- | --- | --- |
| Neutral gradient | admission passes; no veto | `1.67 / 1.99 / 3.14` | provisional; retention rejects inset |
| Gradient + sigma-1 grain | passes; no veto | `2.71 / 3.84 / 6.48` | provisional; retention rejects inset |
| Subpixel-like glyph line (+8 Y) | passes; no veto | `1.67 / 2.09 / 10.31` | provisional; retention rejects inset |
| Faint glyph line (+16 Y) | passes; no veto | `1.68 / 2.09 / 18.15`; 476 small residual components, max area 18 | provisional; retention rejects inset |
| 24 sparse stars | passes; no veto | `1.67 / 1.99 / 301.75` | provisional; retention rejects inset |
| Compact 72-pixel logo | passes; no veto | `1.67 / 1.99 / 65.86`; 4 small residual components, max area 24 | provisional; retention rejects inset |
| Broad smooth neutral +8 object | passes; no veto | `1.83 / 2.34 / 3.59` | provisional; retention rejects inset |
| Structured ceiling/panel | base admission fails; morphology veto | `26.71 / 39.78 / 64.95` | provisional; retention rejects inset |
| Internal letterbox-like band | exterior passes; no veto; outer edges remain 300/1856 | `1.67 / 1.99 / 3.14` | provisional; retention rejects inset |
| Near-boundary subtitle | base admission passes; morphology veto | `1.68 / 2.10 / 161.51`; 488 small residual components, max area 20 | provisional; retention rejects inset |

All ten controls had top/bottom edge scans at outside/inside rows `300/301` and `1857/1856`, with full-column line verification passing. The new morphology rule catches the structured panel and the large boundary subtitle. It permits the faint/subpixel glyphs, sparse stars, compact logo, and broad +8 object. The spline p90/spread summary also passes those captions despite their high residual peaks; that metric alone is not a content-safety decision.

The frozen production-shaped median-17 visibility diagnostic produces thousands of raw samples at `Y > black_threshold+8` for the raised synthetic gradients/objects but zero `R>32` witnesses; the intersection rule `Y > max(black_threshold+8, localMedian17+32)` suppresses those smooth-background counts. It also misses the subpixel/faint glyph, stars, logo, and broad-object controls on the fixed 64x256 sample grid. The near-boundary subtitle produces 141 intersection samples over two adjacent depth rows on the nominal band but 95 samples on only one row on the guarded band. Dense morphology and raw beta retention remain necessary complements; the sampled intersection cannot stand alone.

Risk interpretation

If the new profile evidence were promoted directly into crop authority, the faint/subpixel caption, star, logo, and broad +8 controls would be potential newly introduced false crops relative to unchanged beta, which keeps all ten provisional and rejects their supplied inset geometry. No production regression has shipped. This four-kilopixel broad +8 control is not the earlier 1280x720 broad +8 control: that earlier beta test classified its supplied crop `BAR_CROP_TRUSTED`; this new full-resolution gradient context is beta `PROVISIONAL`, so the earlier shared-blind-spot result does not dismiss this incremental risk.

The neutral gradient and grain are calibration controls, not labeled true crop examples. The structured panel is a distinguishable negative; the internal band checks that outer-edge nomination stays anchored to the raster. Small text/star/logo controls are engineered cases, not real-scene prevalence estimates. The 12x6 spline controls were reused as development challenges after the spline protocol was frozen; they are not a new independent validation set. The 4 ad payloads likewise remain one short repeated clip. None of these measurements establishes a correct user-facing aspect ratio or crop for the ad.

Artifacts

- Real decoder probe and all-pixel cross-check: `native-decode-probe.cpp`, `decoder-crosscheck.py`, `decoder-crosscheck.json`, the three `*.csv` files, and three `*.yuv16` outputs.
- Real-capture morphology/reference/retention data: `frozen-morphology-real-captures.json`, `reference-relative-real-captures.json`, `visibility-intersection-results.json`, `make_previews.py`, and `*-preview.png`.
- Frozen synthetic generator, exact control planes, and hashes: `synthetic/heldout-protocol.txt`, `synthetic/generate_controls.py`, `synthetic/manifest.json`, ten `*.p210` and `*.expected` files.
- Synthetic beta/profile results: `synthetic/beta-results.csv`, `synthetic/classifier-results.json`, `synthetic/reference-relative-results.json`, `synthetic/visibility-intersection-results.json`, and `synthetic/beta-sampler-validation.log`.
- Applied protocol hashes: morphology `BF2566AA3EF12CABF79030F033AEFAF929047E6FD686F1D6C70067DA0955B5D4`; local reference `85ABC26B1C07372098A1851AE6974D3CB2F3D9952C070A7790269A84A1552E93`; spline `FA6FF6ADBEDCF21AFAC2FFD052FB7EEA4BDFDEE1172E40CEAFACE4B8B018562F`.


Audit correction (2026-10-27): the first visibility outputs incorrectly used beta `global_cutoff` as the `IsCrediblyVisible` threshold. Those outputs are preserved as `*.invalid-global-cutoff.json`. Corrected runs use `black_threshold+8`; city-lights remains zero-visible at 32, while Thai/Latin remain at 96. The global nearblack cutoff is separately labeled. The synthetic edge-evidence JSON row labels were corrected: bottom picture row 1856 is inside and exterior row 1857 is outside. Corrected outputs are in the original `reference-relative-real-captures.json`, `visibility-intersection-results.json`, and `synthetic/reference-relative-results.json`.
