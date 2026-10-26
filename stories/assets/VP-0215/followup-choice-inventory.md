# VP-0215 Follow-up Config choices and ownership

Source: 405cdea1987a720666b3d2a87e2c1c4e0c0ff710, continuing requested trial
a37dab1f7c518de7c585dff3c479e84c70038598 on local docking base
eab55b4830877a8ebfb943c40a1d5dacbcf795ae and beta
8c4537462bbabd6ce07ba3e9ffd9be9bc8745492.

| Setting | UI choices/order | Practical default / compatibility |
| --- | --- | --- |
| Image upscaler | EWA Lanczos 4 sharpest, EWA Lanczos sharp, EWA Lanczos, Lanczos, Catmull-Rom, Bicubic, Gaussian, Oversample, Bilinear, Nearest, GPU sampling | EWA Lanczos sharp for new High defaults; 4 sharpest remains opt-in. Preserve old Fast/Balanced policy and explicit values. |
| Image downscaler | Lanczos, Mitchell, Catmull-Rom, Bicubic, Gaussian, Hermite, Bilinear, Box, GPU sampling | Retain existing Hermite fallback and explicit user choices for this trial. No new VP cost or visual benchmark is claimed. |
| Chroma upscaler | EWA Lanczos sharp, EWA Lanczos, Lanczos, Bicubic, Bilinear | Retain Bicubic fallback and explicit EWA choices. Visible Chroma upscaler row in the unified Scaling section. RGB needs no reconstruction. |
| Sigmoid upscaling | On, Off | Existing On fallback; reset removes only this profile's override. |
| Dithering | Blue noise, Ordered (fixed), White noise, Off | Blue noise first and selected by default, with no recommendation suffix. Auto/On/true mean Blue noise even with old Fast. Ordered LUT/error diffusion remain file-compatible, nonselectable legacy values. |
| Debanding | Standard, Light, Off | Existing Standard High fallback. Retain deliberate Off/Light. |
| Smooth motion mode | Off, Cadence mismatch only, Always | Off remains default. Preserve actual auto cadence behavior under a descriptive name. |
| Frame mixer | Clamped Mitchell, Hermite, Linear, Oversample | Oversample remains default independently of ordering. Cubic is legacy-only: the old null-mixer behavior is preserved. Enabling true Cubic exposed an opaque-subtitle regression, so it is omitted from selectable choices. Motion quality tradeoffs depend on cadence and blur tolerance. |
| Deinterlacer | BWDIF, YADIF, Bob, Weave | BWDIF remains default; Weave is a bypass. |
| Deinterlace mode | Interlaced input only, Off, Force | Interlaced input only remains default and requires known field order. |
| Field order | From input, Top field first, Bottom field first | From input; do not freeze automatic metadata. |

Generic profile omission rows are removed from menus after resolving the
selected value; file omission remains supported for inheritance. Concrete inherited values are selected directly. A Reset
override action removes the local value; selecting the same inherited value
explicitly pins it. True adaptive policies remain intact.

Scaling / Quality contains one Scaling section with image upscaler, downscaler,
sigmoid and chroma upscaler. Debanding / Dithering contains debanding, dithering
and target depth. Tone Mapping has no Processing overrides section or adoption
button. Its one Advanced tone mapping toggle reads Basic tone mapping while
active; returning restores the previous expanded sections, docking and scroll.

Shared parser/editor migration moves legacy Rendering processing keys into
visible Scaling profiles and removes the old owner. Rendering's former higher
precedence is preserved; distinct named values reuse matching Scaling profiles
or create visible named destinations, while redundant values avoid extra copies.
Rules/shortcuts/labels are copied when relevant and absent at the destination.
Old removed dithering algorithms remain readable, with friendly legacy labels.
Inline comments remain preserved when the legacy setting moves. The file is
unchanged until Save/Apply, which keeps an original-file migration backup.
Independent cross-family selection combinations still need review; migration
does not claim to preserve every old selection cross-product automatically.

Named Tone Mapping profiles receive the effective inherited peak-detection and
contrast-recovery values in the same migration. Explicit Auto keeps its reset
meaning. The migrated file must reopen cleanly, including Cancel and remote
Apply cases, without scheduling a second defaults migration.

Actual defaults, quality ordering and cost differ: the list is not a benchmark
ranking. The user requested 4 sharpest ahead of sharp; representative live VP
quality/cost qualification remains before final story acceptance. No exact
cost multiplier is asserted. Existing operator values and active configuration
are preserved at deployment.
