# Relative-edge grid nomination experiment

This is a diagnostic-only experiment against source commit `7394ba420999a8fa708718ee4ce4d87e7459369f`. It leaves the VP source tree unchanged and never returns crop authority. Implementation is `boundary-grid.cpp`; inputs are the existing 97-frame capture at `C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1`. It reuses the beta `SampleActivePictureDiagnosticGrid` (128 columns × 270 rows), linked from the baseline object files; there are no extra full-image passes or per-pixel reads after grid construction.

## Fixed protocol

For top and bottom independently, scan only the outer half of the sampled rows. Pair each outer sample row with the adjacent inward row; a column supports a step when inward 10-bit Y minus outward Y is at least 24. Require at least 80% of 32 sampled columns in each of four horizontal quartiles (26/32 minimum). Record, but do not require, chroma corroboration where either |dU| or |dV| is at least 10. At candidate crossing, require all quartile medians at each of 2, 4, and 8 sampled rows farther inward to exceed that quartile's median from the first two outer rows by at least 16 codes. The first crossing from the edge satisfying all requirements is returned.

The nominal measured coordinate is the midpoint of the adjacent sampled-row coordinates. The bracket is the actual outside/inside pair. A separate conservative retained-fringe interval extends the bracket by one grid pitch (~8 pixels) on each side. The fringe is uncertainty/display-containment only and is not used as the source-aspect measurement. No fixed aspect, mirrored-edge assumption, black absolute floor, temporal voting, authority, exterior-safety conclusion, or caption envelope is used.

## Captured result

All 97 captured frames nominated both edges with the same coarse brackets:

- Top: sampled pair 296/304, nominal midpoint y=300, conservative fringe [288, 312].
- Bottom: sampled pair 1862/1854 (scan is from the lower edge inward), nominal midpoint y=1858, conservative fringe [1846, 1870].
- All four quartiles had 32/32 positive-Y supports on both boundaries; all 32/32 pairs in each quartile also had optional UV corroboration. All three persistence depths passed.
- Nominal midpoint aperture is 3840×1558, about 2.465:1. It is a coarse candidate measurement only; it is not a safety or authority result. The recorded red-panel transition is around y=302/1858, so the coarse top midpoint differs by about 2 px.

Per frame, the grid sampler produced exactly 34,560 samples (97 frames: 3,352,320 samples). The relative scan evaluated 943,616 adjacent pairs total (~9,728/frame, including early edge exit). This run's measured nomination time, including grid sampling and relative scoring, averaged ~540 µs/frame; this is a concurrent exploratory timing, not a runtime benchmark.

## Synthetic controls and false positives

`synthetic.csv` contains these 3840×2160 BGRA8 test images, passed through the same real grid sampler and scorer:

- A top-gradient/exterior plus bright interior, and a low-luma exterior (gray 8) against dark gray-22 picture: both nominated both edges. This shows the relative rule does not need an absolute black floor.
- Flat full-raster, uniform full-raster, an 8-pixel thin bright line, and an internal divider with no edge bands: no pair was nominated.
- A localized bright caption inside the bottom exterior band did not change the edge nomination. This says nothing about whether that band is safe to discard or whether glyph presentation is preserved.
- `authored_full_raster_scene_bands_pixel_alias` is byte-for-byte equivalent in its sampled image to the edge-shaped synthetic but semantically represents authored full-raster scene content. It nominates the same top and bottom edges. This is an explicit semantic false positive: these local pixels cannot distinguish a matte from authored scene content.

## Interpretation

This demonstrates that the bounded grid can nominate useful coarse, straight, full-width transition intervals on the captured ad; it does not establish that either exterior band is expendable. A production design still needs an independent, complete qualification of every excluded outside band and reconciliation against current visible/source pixels before temporal confirmation can publish anything. More repeated votes do not make unsafe outside-band pixels safe. Keep nominal source-aspect edges separate from conservative fringe and from the caption-safe envelope; the subtitle/visible envelope must not redefine aspect. The authored-content alias is the key negative control against promoting this diagnostic to authority.

Files: `boundary-grid.cpp`, `build.log`, `run.log`, `candidate.csv`, `synthetic.csv`.

