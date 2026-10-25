# VP-0215: Clarify dithering and motion configuration choices

## Status

Backlog

2026-10-25: Research and story requested by the user; implementation has not
started. The user clarified that dithering needs one method selector: Off
disables it and choosing any method enables it. A separate enable switch is
not wanted. The user's follow-up removes Auto and On from the menu: Blue noise
is first/default, and existing Auto or On selections resolve to Blue noise.
Expose the other three ordinary methods alongside Off; keep the ten
error-diffusion variants under Advanced and retain advanced saved settings.

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
moving video. Keep error diffusion out of the everyday video list; the three
ordinary alternatives remain directly selectable with short explanatory help.
See the [dithering API](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/shaders/dithering.h)
and [options reference](https://libplacebo.org/options/#error_diffusionkernel).

VP already accepts four ordinary methods and ten error-diffusion kernels.
Its runtime maps legacy `on`/`true` to `blue_noise` and `off`/`false` to `off`.
At the researched baseline Auto follows VP's quality preset: Blue noise for
Balanced/High and Off for Fast. The user explicitly replaces that policy in
this story: legacy Auto resolves to Blue noise at every quality level. This
intentionally changes old Auto + Fast from Off to Blue noise. Legacy On/true
also resolves to Blue noise; explicit Off/false remains Off. Blue noise is the
root default when omitted; omission in a named profile still inherits its
parent. Inheritance remains a separate profile operation.

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

1. Keep a single Dithering selector with the following normal choices in this
   order. Blue noise is first and is the root default. No Auto or On entries,
   separate enable checkbox, or dithering status implying quality-preset control.

| Stored token | Normal display label |
| --- | --- |
| `blue_noise` | Blue noise (recommended) |
| `ordered_fixed` | Ordered (fixed) |
| `ordered_lut` | Ordered (LUT) |
| `white_noise` | White noise |
| `off` | Off |

2. Provide a collapsed/opt-in **Advanced methods** disclosure for the same
   selector. It reveals only the ten error-diffusion variants below, without
   a second active method control or a new renderer setting. Viewing more
   choices must not dirty or save the config. If an existing config selects an
   advanced method, show its friendly selected value immediately and make that
   choice available without requiring discovery of the disclosure first.

| Stored token | Advanced display label |
| --- | --- |
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
   verified for VP's shipped runtimes. Explain fixed ordered's speed, Ordered
   LUT's mainly testing-oriented purpose, and White noise's more visible noise.
   Preserve all explicit accepted methods and recognize legacy aliases. Saved
   Auto/On/true, including mixed case, display and resolve as Blue noise in both
   editor and runtime/headless config loading. Normalize them to `blue_noise`
   when saving/applying the affected settings; merely opening or cancelling
   the editor must not rewrite files. New selections write canonical existing
   tokens. Do not implement this as a label-only change while leaving Auto
   dependent on quality at runtime.
4. Use one semantic label mapping keyed by section/key/value, shared by menu
   construction, saved-value presentation, inheritance summaries, and reloads.
   Bind labels to stored item data rather than numeric positions. Preserve
   canonical lowercase tokens, comments, unknown keys, profile ownership, and
   existing save/apply behavior. Use **Inherit from default profile** where
   available; inheritance must remain distinct from explicit Blue noise or Off.
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
  No Auto/On entry, duplicate effective methods, or raw snake_case method labels
  appear in dithering menus or inheritance summaries. Verify the five normal
  choices and their order, with Blue noise first.
- Legacy Auto/On/true and mixed-case spellings resolve to Blue noise, including
  old Auto + Fast, through both runtime loading and editor migration. Apply/OK
  and reopen retain canonical `blue_noise`. Open/Cancel and merely viewing
  advanced choices do not rewrite files. All 14 explicit methods and Off retain
  their behavior. An omitted root uses Blue noise; an omitted named value
  follows its parent, including an explicit inherited Off. Repeated profile
  switching must not append retired Auto/On choices or dirty unrelated values.
- Check both libplacebo variants and all VP quality presets: Blue noise and its
  retired aliases never resolve to a preset's Off/error-diffusion path; ordinary
  vs error-diffusion parameter projection remains correct; Off clears both
  paths; and the resolved method agrees with the label. Preserve target depth
  and calibration-LUT order.
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
correction. The user-authorized dithering change makes Blue noise the root
default and maps retired Auto/On to Blue noise regardless of quality, including
old Auto + Fast. Preserve other quality defaults. No new dithering algorithm,
libplacebo upgrade, capture/deinterlacing policy change, RIFE implementation,
or deployment is included. Keep VP-0032/VP-0033's existing review states.

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
