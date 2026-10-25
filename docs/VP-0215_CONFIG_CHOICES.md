# VP-0215 Config choice inventory and trial defaults

Base: eab55b4830877a8ebfb943c40a1d5dacbcf795ae (local Config docking integration),
on beta 8c4537462bbabd6ce07ba3e9ffd9be9bc8745492.

| Setting | UI choices/order | Practical default / compatibility |
| --- | --- | --- |
| Image upscaler | EWA Lanczos 4 sharpest, EWA Lanczos sharp, EWA Lanczos, Lanczos, Catmull-Rom, Bicubic, Gaussian, Oversample, Bilinear, Nearest, GPU sampling | EWA Lanczos sharp for new High defaults; 4 sharpest remains opt-in. Preserve old Fast/Balanced policy and explicit values. |
| Image downscaler | Lanczos, Mitchell, Catmull-Rom, Bicubic, Gaussian, Hermite, Bilinear, Box, GPU sampling | Retain existing Hermite fallback and explicit user choices for this trial. No new VP cost or visual benchmark is claimed. |
| Chroma upscaler | EWA Lanczos sharp, EWA Lanczos, Lanczos, Bicubic, Bilinear | Retain Bicubic fallback and explicit EWA choices. Visible Chroma scaling section. RGB needs no reconstruction. |
| Sigmoid upscaling | On, Off | Existing On fallback; reset removes only this profile's override. |
| Dithering | Blue noise, Ordered (fixed), White noise, Off | Blue noise first/recommended. Auto/On/true mean Blue noise even with old Fast. Ordered LUT/error diffusion remain file-compatible, nonselectable legacy values. |
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

Image scaler controls exist only on Scaling. The local Rendering debanding,
dithering and depth overrides are retained. Provably redundant Rendering image
scalers (equal under every Scaling selection) are removed in the shared parser/
editor migration. Conflicts retain their runtime owner and get a visible notice
on Scaling plus an explicit Use Scaling settings action. This does not copy
shortcuts/rules between independent profile families or silently change their
precedence. The trial does not claim full automatic conversion of conflicting
legacy files.

Actual defaults, quality ordering and cost differ: the list is not a benchmark
ranking. The user requested 4 sharpest ahead of sharp; representative live VP
quality/cost qualification remains before final story acceptance. No exact
cost multiplier is asserted. Existing operator values and active configuration
are preserved at deployment.
