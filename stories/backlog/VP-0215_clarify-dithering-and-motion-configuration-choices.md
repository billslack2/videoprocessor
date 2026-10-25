# VP-0215: Clarify dithering and motion configuration choices

## Status

Backlog

2026-10-25: Research and story requested by the user; implementation has not
started. The user clarified that dithering needs one method selector: Off
disables it and choosing any method enables it. A separate enable switch is
not wanted. Keep the everyday list short and retain advanced saved settings.

Next action: reproduce fresh/legacy profile menus on the then-current beta,
prove the Cubic mixer mismatch below with a production-path regression, and
implement the naming/choice cleanup with an explicit Cubic compatibility decision.

## Problem

The supplied screenshot shows raw dithering tokens, ten error-diffusion
variants, and On/Off alongside Auto and named algorithms. It does not explain
what On selects or why a video user would choose these methods. Smooth motion
and deinterlacing also mix friendly labels, lowercase tokens, and acronyms.
The user wants researched, technically accurate choices and consistent names.

Screenshot: [reported dithering menu](../assets/VP-0215/reported-dithering-menu.png).
Treat this image as observed UI evidence. Its deployed source identity was not
established; do not assume it exactly represents the latest beta menu.

## Research baseline and findings

GitHub discovery on 2026-10-25 identified `v1.4.02-beta` as both the latest beta
integration branch and repository default. Source inspection used its fetched
tip `8c4537462bbabd6ce07ba3e9ffd9be9bc8745492`, in a clean detached worktree at
`E:\codex\videoprocessor\config-choice-research-20261025`. No source changed.
Both shipped libplacebo source archives were inspected: stable 7.360.1/API360
and beta 7.377.0/API377. Revalidate these findings against the current beta when
implementation begins.

### Dithering

Libplacebo's preferred/default method is blue noise. Ordered LUT is primarily
useful for testing; fixed ordered dithering is a faster alternative. White
noise is cheap but can look more objectionable. Error diffusion is a separate,
costly path that upstream recommends mainly for still images rather than
moving video. These alternatives do not belong in the everyday video list.
See the [dithering API](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/shaders/dithering.h)
and [options reference](https://libplacebo.org/options/#error_diffusionkernel).

VP already accepts four ordinary methods and ten error-diffusion kernels.
Its runtime maps legacy `on`/`true` to `blue_noise` and `off`/`false` to `off`.
Auto follows VP's quality preset: Blue noise for Balanced/High and Off for Fast.
It is not another spelling of explicit Blue noise. Inheritance is a separate
profile operation: remove the local override and follow the parent.

The current editor already attempts friendly dither labels using numeric item
positions. Its shared `friendlyChoiceLabel` lacks these method mappings, and
profile loading can append previously saved values absent from the initial
list. This provides a path for legacy On to reappear and for raw tokens to
leak into inherited/saved-value labels. It does not establish an unconditional
On/Off injection bug. Reproduce the screenshot across fresh settings, legacy
values, and repeated profile changes before claiming a precise root cause.

### Smooth motion: a functional completeness gap

VP validates/offers `oversample`, `hermite`, `linear`, and `cubic`.
`TemporalRenderer` resolves them by exact name in `pl_frame_mixers`.
Both bundled libraries have the same recommended compatibility registry:

| Registry token | Meaning |
| --- | --- |
| `none` | No mixing; represented in VP by Smooth motion Off |
| `oversample` | Oversampling; libplacebo's default frame mixer |
| `linear` | Linear frame mixing, backed by the bilinear filter |
| `hermite` | Hermite cubic spline |
| `mitchell_clamp` | Clamped Mitchell spline |

VP omits `mitchell_clamp`, while `cubic` is absent from this registry. The
production lookup therefore leaves Cubic's mixer pointer null; libplacebo's
renderer contract defines null as no mixing/nearest-frame behavior. This is a
source-established mismatch, not a live-playback reproduction. A separate
`cubic` filter exists in the newer filter configuration API; its existence
does not make the older registry lookup succeed. Do not rename Cubic to
Hermite, Bicubic, or Clamped Mitchell as if these were interchangeable.

The [upstream registry](https://github.com/haasn/libplacebo/blob/master/src/renderer.c)
confirms the same recommended set. The broader set of filters allowed for
temporal use is not the same as the recommended list. This story should cover
the recommended set without importing every spatial upscaler into this menu.

### Deinterlacing: algorithms are complete

VP exposes all four algorithms in both bundled headers: BWDIF, YADIF, Bob,
and Weave. Technical acronym names are appropriate; Bob is a name, not an
acronym. Weave preserves fields without deinterlacing. Mode, field order,
and output cadence are separate VP policies, not extra algorithms.

Preserve VP's current BWDIF default. The bundled `PL_DEINTERLACE_DEFAULTS`
macro selects BWDIF, although nearby API prose and the website still mention
YADIF as the default/recommendation. Document this version difference rather
than changing behavior based on stale prose. See the
[deinterlacing header](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/shaders/deinterlacing.h).

## Intended UI and compatibility contract

1. Keep a single Dithering selector. Normal choices are **Auto (follow quality
   preset)**, **Blue noise (recommended)**, and **Off**. No separate enable
   checkbox and no selectable legacy On alias. Show Auto's effective method
   using the existing quality-resolution status.
2. Provide a collapsed/opt-in **Advanced methods** disclosure for the same
   selector. It reveals the remaining methods below, without a second active
   method control or a new renderer setting. Viewing more choices must not
   dirty or save the config. If an existing config selects an advanced method,
   show its friendly selected value immediately and make that choice available
   without requiring the user to discover the disclosure first.

| Stored token | Advanced display label |
| --- | --- |
| `ordered_fixed` | Ordered (fixed) |
| `ordered_lut` | Ordered (LUT) |
| `white_noise` | White noise |
| `error_diffusion_simple` | Error diffusion: Simple |
| `error_diffusion_false_fs` | Error diffusion: False Floyd-Steinberg |
| `error_diffusion_sierra_lite` | Error diffusion: Sierra Lite |
| `error_diffusion_floyd_steinberg` | Error diffusion: Floyd-Steinberg |
| `error_diffusion_atkinson` | Error diffusion: Atkinson |
| `error_diffusion_jarvis_judice_ninke` | Error diffusion: Jarvis-Judice-Ninke |
| `error_diffusion_stucki` | Error diffusion: Stucki |
| `error_diffusion_burkes` | Error diffusion: Burkes |
| `error_diffusion_sierra2` | Error diffusion: Sierra 2 |
| `error_diffusion_sierra3` | Error diffusion: Sierra 3 |

3. Brief help explains which methods are recommended for video, the cost and
   still-image use case for error diffusion, and capability/fallback behavior
   verified for VP's shipped runtimes. Preserve all existing accepted methods
   and aliases. A saved On displays as Blue noise with its legacy meaning;
   loading/reviewing it alone must not rewrite the config. Newly selecting a
   method writes its canonical existing token.
4. Use one semantic label mapping keyed by section/key/value, shared by menu
   construction, saved-value presentation, inheritance summaries, and reloads.
   Bind labels to stored item data rather than numeric positions. Preserve
   canonical lowercase tokens, comments, unknown keys, profile ownership, and
   existing save/apply behavior. Use **Inherit from default profile** where
   available; do not conflate inheritance with Auto or Off.
5. Smooth motion uses **Off**, **Auto (cadence mismatch only)**, **Always**;
   enabled filter labels use **Oversample**, **Hermite**, **Linear**, and
   **Clamped Mitchell**. Add `mitchell_clamp` consistently to validation, UI,
   documentation, and the production lookup. Keep Off in the mode selector
   rather than adding a redundant `none` filter. Preserve oversample as default.
6. Resolve Cubic explicitly before finalizing the filter list. Prefer a true
   supported temporal Cubic mapping if available in both shipped runtimes,
   with kernel/radius and output tests. Otherwise retain old configs without
   presenting a nonfunctional choice as supported, clearly describe their
   effective legacy behavior, and record the migration decision. Do not silently
   substitute an unrelated kernel or drop saved values. No offered enabled
   filter may silently resolve to a null mixer.
7. Deinterlacing labels are **Auto (from input)**, **Off**, **Force**; algorithms
   are **BWDIF**, **YADIF**, **Bob**, **Weave**. Keep **From input**, **Top field
   first**, **Bottom field first**, **Field rate (preserve motion)**, and
   **Frame rate** consistent. Give the YADIF-only spatial-skip boolean clear
   False/True labels and explanatory help; preserve its stored boolean semantics
   and existing applicability rules.
8. Perform a bounded scan of adjacent renderer choice controls for the same
   raw-token/alias/label-position defect. Include directly comparable naming
   fixes with evidence; track unrelated config redesign separately.

## Acceptance criteria

- Fresh/default and named-profile menus satisfy the normal/advanced contract.
  No ambiguous On entry, duplicate effective methods, or raw snake_case method
  labels appear in the affected menus or inheritance summaries.
- Legacy On, mixed-case aliases, all 14 explicit dither methods, Off, Auto,
  omitted/inherited values, and repeated profile switching keep their effective
  behavior. Open/Cancel and merely viewing advanced choices do not alter files;
  explicitly selecting a value, Apply, OK, and reopen preserve the selection.
- Check both libplacebo variants: Auto under all VP quality presets; ordinary
  dither vs error-diffusion parameter projection; Off clears both paths; and
  the resolved method agrees with the operator-facing label. Preserve target
  depth and calibration-LUT order.
- Production-path mixer tests reproduce Cubic's old failed lookup, prove each
  final offered filter resolves correctly, and verify nontrivial temporal
  weights/output and required buffering, rather than only checking UI strings.
  Cover Clamped Mitchell and the chosen Cubic compatibility behavior on both
  runtimes. Smooth motion remains frame blending, not motion-compensated frame
  generation.
- Confirm all four deinterlacers, mode/field-order/cadence choices, metadata
  handling, and YADIF-only control enablement retain their existing semantics.
- Update relevant configuration help/reference and public-value catalogs to
  distinguish current menu choices from legacy accepted spellings. Include
  readable screenshots of normal/advanced dithering and motion/deinterlacing.
- Run the relevant ConfigEditorWindow, ConfigEditorCore/config-validation,
  LibplaceboRenderParameters, and MotionRenderer regression coverage plus a
  successful x64 Release build. Record any untested interactive/GPU coverage.

## Boundaries and evidence locations

This is a naming/choice cleanup plus the directly discovered mixer mapping
correction. It does not change quality defaults, introduce a new dithering
algorithm, upgrade libplacebo, change capture/deinterlacing policies, implement
RIFE, or deploy anything. Keep VP-0032/VP-0033's existing review states.

Primary source touchpoints at the researched commit:

- `src/VideoProcessor-Config/ConfigEditorWindow.cpp`: friendlyChoiceLabel,
  profile addChoice/loading, Processing and Motion sections (approximately
  lines 870, 5301, 6780, 6844, 7759).
- `src/VideoProcessor-Lib/RendererProfileConfig.h`: motion/scaling validation
  (approximately lines 639-679).
- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`: ReadDithering
  and display-rule parsing (approximately lines 1727 and 1856).
- `src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.cpp`: dithering
  projection/resolution (approximately lines 93, 117, 313).
- `src/VideoProcessor-Lib/vprenderer/TemporalRenderer.h`: mixer lookup and
  render submission (approximately lines 239-242 and 756-783).
- Both `3rdparty/libplacebo*/source/` archives: `src/renderer.c`, `src/filters.c`;
  matching installed `filters.h`, `renderer.h`, and shader headers.
- `docs/configuration-public-values.tsv`, `CONFIGURATION.html`, and existing
  ConfigEditorWindowTests, ConfigEditorCoreTests, LibplaceboRenderParametersTests,
  and MotionRendererTests.

Tracker audit before assignment: 233 canonical records and 233 table rows,
maximum root VP-0214, no duplicate/missing IDs, child gaps, or registry/count
conflicts. Existing unrelated discrepancies: VP-0168 index casing differs from
its folder, VP-0070 lacks the expected Status heading, and 209 historical status
lines are not exact standalone state names. Deliberate repair decision for this
story: preserve those records and defer historical normalization to dedicated
tracker maintenance; do not silently repair them as part of VP-0215.
