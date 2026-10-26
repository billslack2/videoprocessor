# VP-0215 Config choice inventory and trial defaults

Base: eab55b4830877a8ebfb943c40a1d5dacbcf795ae (local Config docking integration),
on beta 8c4537462bbabd6ce07ba3e9ffd9be9bc8745492.

| Setting | UI choices/order | Practical default / compatibility |
| --- | --- | --- |
| Image upscaler | EWA Lanczos 4 sharpest, EWA Lanczos sharp, EWA Lanczos, Lanczos, Catmull-Rom, Bicubic, Gaussian, Oversample, Bilinear, Nearest, GPU sampling | EWA Lanczos sharp for new High defaults; 4 sharpest remains opt-in. Preserve old Fast/Balanced policy and explicit values. |
| Image downscaler | Lanczos, Mitchell, Catmull-Rom, Bicubic, Gaussian, Hermite, Bilinear, Box, GPU sampling | Retain existing Hermite fallback and explicit user choices for this trial. No new VP cost or visual benchmark is claimed. |
| Chroma upscaler | EWA Lanczos sharp, EWA Lanczos, Lanczos, Bicubic, Bilinear | Retain Bicubic fallback and explicit EWA choices. Chroma is in the combined Scaling section. RGB needs no reconstruction. |
| Sigmoid upscaling | On, Off | Existing On fallback; reset removes only this profile's override. |
| Dithering | Blue noise, Ordered (fixed), White noise, Off | Blue noise first, without a recommendation suffix. Auto/On/true mean Blue noise even with old Fast. Ordered LUT/error diffusion remain file-compatible, nonselectable legacy values. |
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

Scaling / Quality now has one combined Scaling section for image and chroma
controls, plus Debanding / Dithering. Tone Mapping replaces Rendering / DTM;
its Processing overrides section is removed completely. Existing Rendering
processing values load into canonical Scaling profiles, merging by name and
using the old higher-precedence values on conflict. Default Rendering processing
values are applied to existing Scaling profiles because they previously took
precedence over those profiles. Nonredundant named values get visible Scaling
profiles; applicable labels/rules/shortcuts are copied when not already present.
Legacy error-diffusion values are retained, never reintroduced as selectable
methods. Inline comments survive removal of the old entries. Loading/cancelling
does not write the file; Save/Apply persists the migration with a backup.
Independent profile selections require review after moving ownership; no exact
mapping is promised for previously conflicting independent selectors.

Advanced tone mapping has one toggle. It reads Advanced tone mapping in basic
mode and Basic tone mapping while expanded. Manual navigation/profile docking
choices are preserved; there is no additional Restore panels button.

Actual defaults, quality ordering and cost differ: the list is not a benchmark
ranking. The user requested 4 sharpest ahead of sharp; representative live VP
quality/cost qualification remains before final story acceptance. No exact
cost multiplier is asserted. Existing operator values and active configuration
are preserved at deployment.
