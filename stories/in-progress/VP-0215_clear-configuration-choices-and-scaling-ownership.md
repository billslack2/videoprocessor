# VP-0215: Make configuration choices clear and consolidate scaling

## Status

In Progress

2026-10-25: User authorized implementation and trial deployment on the local Config changes. The user's
scope includes configuration choices throughout the editor, not just dithering:
consistent names, concrete default selections, sensible quality ordering,
practical recommended defaults, one place to edit scaling, and visible chroma
scaling. Remove generic Auto/Use default choices while preserving real adaptive
policies under descriptive names. Keep the agreed four dithering choices.

Next action: inventory the editor's choices and their effective default/override
semantics, reproduce the supplied Rendering/DTM duplicate-scaling screenshot
with the deployed editor identity recorded, compare a clean current-beta build,
and settle the quality/default matrix and Cubic compatibility decision below.
Then implement and validate the shared UI and ownership corrections.


### Readiness and implementation base

2026-10-25: GitHub default/current beta rechecked as v1.4.02-beta, fetched
8c4537462bbabd6ce07ba3e9ffd9be9bc8745492. The user explicitly selected the local
Config work as the integration base: clean eab55b4830877a8ebfb943c40a1d5dacbcf795ae
on codex/pr177-left-docking-20261025, already deployed by the preceding Config
session. This includes retired-quality migration and intentionally retained
Rendering processing overrides, superseding parts of the earlier beta research.
New isolated implementation branch: codex/vp0215-config-choices-20261025;
worktree: E:\codex\videoprocessor\vp0215-config-choices-20261025.

Readiness: shared QualityProfileMigration runs in editor and parser; profile
choice loading already knows inherited effective values; renderer projection
owns final dithering and plane_upscaler; TemporalRenderer owns mixing. Preserve
left docks, charts and local migration work. Tests will cover concrete choices,
legacy ownership, inheritance/reset, runtime aliases and mixer resolution, with
x64 Release build and matched host/stable/beta renderer deployment. Active
configuration will be backed up and preserved during binary replacement.
Implementation remains subject to the story's migration and validation checks;
no unverified quality/cost measurements are claimed.

## Problem and reported UI evidence

The UI exposes raw tokens such as `linear`, `cubic`, `weave`, and `Default:
bwdif`, mixes them with BWDIF/YADIF/BOB, and repeats a concrete algorithm beside
Auto or Use default. A user should see the actual chosen algorithm without
having to understand configuration omission or preset plumbing. The highest
quality option can appear first without becoming the automatic default when
its cost is too high for normal video use.

The user also confirms that Rendering/DTM -> Processing overrides exposes
Upscaler, Downscaler, and Sigmoid upscaling even though Scaling has its own
page. That is the reported duplication. Chroma scaling is difficult to find.

Supplied screenshots:

- [Dithering choices](../assets/VP-0215/reported-dithering-menu.png).
- [Motion mixer names and Use default](../assets/VP-0215/reported-motion-mixer-menu.png).
- [Deinterlacing names and Default: bwdif](../assets/VP-0215/reported-deinterlacing-menu.png).
- [EWA Lanczos ordering](../assets/VP-0215/reported-upscaler-menu.png).
- [Duplicate scaling controls in Rendering/DTM](../assets/VP-0215/reported-rendering-scaling-overrides.png).

These are observed UI evidence. Their deployed executable/source identity has
not been established; do not assume they exactly match the current beta code.

## Research baseline

GitHub discovery and refresh on 2026-10-25 identified `v1.4.02-beta` as current
beta and repository default. Its fetched tip remained
`8c4537462bbabd6ce07ba3e9ffd9be9bc8745492`. Research used the clean detached
worktree `E:\codex\videoprocessor\config-choice-research-20261025`; no source
changed. Both shipped source archives were inspected: libplacebo stable
7.360.1/API360 and beta 7.377.0/API377. Revalidate on the latest beta when
implementation begins.

### Shared default presentation and scaling ownership

`ConfigEditorWindow::addChoice` inserts an empty inherited/not-set row;
profile loading presents Default/Use default/Inherited labels separately from
real values. Some controls also add AUTO. Friendly labels are incomplete and
some are assigned using numeric positions, so saved/inherited presentation and
menu construction can disagree.

The current source defines one Scaling tab and creates Upscaler, Downscaler,
Sigmoid upscaling, and Chroma upscaler under `vprenderer.scaling` (approximately
lines 6890-6921). Chroma upscaler is currently below Sigmoid. It accepts Bicubic,
Bilinear, Lanczos, EWA Lanczos, and EWA Lanczos sharp; current fallback is
Bicubic. Projection assigns it to libplacebo `plane_upscaler`, distinct from
the full-image upscaler, and it applies to subsampled input even at native
image resolution. RGB input does not require chroma reconstruction.

Unlike the supplied deployed screenshot, this beta source already omits those
three scaler controls from the Rendering form and has
`migrateSeparatedRendererProfiles` to transfer old Rendering scaler settings.
Investigate the editor binary/version, build/package provenance, and migration
path before writing a duplicate source fix. The reported UI remains an
acceptance failure until a verified delivered editor has one canonical owner.
The separate Rendering quality preset influences several defaults; it is not
a second place to edit individual scaling algorithms.

### Dithering

Blue noise is libplacebo's preferred/default method. Ordered LUT is mainly for
testing; fixed ordered is faster; White noise is cheap but more visibly noisy.
Error diffusion is costly and recommended mainly for still images. Per the
user, omit Ordered LUT and all ten error-diffusion variants from every UI
selection list; do not move them to an Advanced menu.
See the [dithering API](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/shaders/dithering.h)
and [options reference](https://libplacebo.org/options/#error_diffusionkernel).

VP's old runtime maps On/true to Blue noise and Off/false to Off. Old Auto
follows quality: Blue noise under Balanced/High and Off under Fast. The user
explicitly replaces this dithering policy: Auto/On/true resolve to Blue noise
at every quality level. This intentionally changes old Auto + Fast. An omitted
root dithering setting also uses Blue noise; an omitted named-profile setting
continues to inherit its parent, including Off.

### Motion and deinterlacing

VP validates/offers `oversample`, `hermite`, `linear`, and `cubic`, then resolves
by exact name in the older `pl_frame_mixers` registry. Both shipped libraries
have this same registry:

| Token | Meaning |
| --- | --- |
| `none` | No mixing; represented by VP's Smooth motion Off |
| `oversample` | Oversampling, the default mixer |
| `linear` | Linear frame mixing, using the bilinear filter |
| `hermite` | Hermite cubic spline |
| `mitchell_clamp` | Clamped Mitchell spline |

VP omits Clamped Mitchell and offers Cubic, which is absent from this registry.
The production lookup leaves Cubic's pointer null; libplacebo defines null as
no mixing/nearest-frame behavior. This is source evidence, not a live playback
reproduction. The newer filter API has a separate temporal Cubic filter; that
does not make the older lookup succeed. Do not rename Cubic to Hermite,
Bicubic, or Clamped Mitchell as if those algorithms were interchangeable.
See the [upstream registry](https://github.com/haasn/libplacebo/blob/master/src/renderer.c).

All four deinterlacers are present: BWDIF, YADIF, Bob, Weave. Both bundled
`PL_DEINTERLACE_DEFAULTS` macros select BWDIF, despite nearby prose/website
still mentioning YADIF. Preserve the actual BWDIF default. Weave preserves the
fields without deinterlacing; field order and cadence are separate policies.
See the [deinterlacing header](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/shaders/deinterlacing.h).

### Quality order and cost are separate from the default

The current High preset resolves image upscaling to EWA Lanczos sharp;
Balanced resolves to Lanczos and Fast to built-in sampling. Downscaling
currently resolves to Hermite for High/Balanced. These are starting points for
the default review, not reasons to preserve every old fallback unexamined.

For the user's example, EWA Lanczos 4 sharpest can be listed ahead of EWA
Lanczos sharp without becoming the recommended/default choice. It has a larger
kernel and built-in antiringing. Upstream mpv describes it as slightly slower
than EWA Lanczos sharp and warns about ringing; neither an exact VP cost nor
"super expensive" has been established. Research and VP timing should support
any cost annotation. Sharpness alone is not a universal quality score.
Sources: [libplacebo filter definitions](https://github.com/haasn/libplacebo/blob/master/src/filters.c),
[mpv scaler descriptions](https://github.com/mpv-player/mpv/blob/master/DOCS/man/options.rst).

## Intended design

### Consistent labels and concrete defaults throughout the editor

- Audit every configuration page and choice menu, including General, Rendering,
  Color/Output, Scaling, Motion, input processing, and named-profile variants.
  Use one semantic label mapping keyed by setting and stored value, bound to
  item data rather than numeric indices. Keep stored tokens compatible.
- Show real values once. Remove generic selectable Auto, Use default, Default,
  Default: value, and empty/inherited placeholder rows from setting menus.
  `Default: bwdif` must become the actual **BWDIF** row selected; `linear` and
  `weave` become **Linear** and **Weave**, and Bob is **Bob**, not **BOB**.
  Preserve acronyms such as BWDIF, YADIF, EWA, LUT and RGB.
- For omitted or legacy preset-based values, display the concrete effective
  algorithm/number/toggle selected in the normal control. Do not merely remove
  Auto and accidentally select the first row. Changing quality or a parent
  profile updates controls still following that policy. Actual saved overrides
  remain explicit and are not overwritten by a preset change.
- Preserve genuine adaptive behaviors using descriptive labels, without bare
  Auto. Examples: smooth motion `auto` -> **Cadence mismatch only**;
  deinterlacing `auto` -> **Interlaced input only**; field order `auto` ->
  **From input**; detected display/source choices describe what they follow.
  Keep their runtime detection, cadence, metadata and fallback behavior. Never
  freeze detected colorimetry, display timing, or field order to today's value.
- Keep named-profile inheritance, outside the value list: show the effective
  normal selection and concise inherited-source status with a **Reset override**
  action. Selecting a concrete value creates an override, even when it equals
  the currently displayed inherited value. Resetting removes that override and
  immediately shows the parent's effective value. Opening, switching profiles,
  or cancelling must not materialize every displayed default into the file.
  A profile actually named Default may keep its name; that is not a setting
  placeholder.

### Quality ordering and recommended defaults

- For algorithms with a meaningful quality comparison, list highest expected
  perceived quality first, with weaker/cheaper alternatives later and Off last.
  Establish a defensible order for image upscaling/downscaling, chroma, motion,
  and deinterlacing based on their intended use, artifacts and performance.
  Record content-dependent ties/tradeoffs rather than equating slower or sharper
  with universally better. Modes, monitors and transfer contracts use meaningful
  semantic ordering rather than a fabricated quality hierarchy.
- Default selection is independent of list position: choose the best practical
  quality without unusually high video-rendering cost. Mark that actual option
  Recommended; do not add a duplicate default row. Expensive high-end choices
  may be first while a lower-cost option is initially selected.
- Specifically keep **EWA Lanczos 4 sharpest** above **EWA Lanczos sharp** in
  the upscaler list; leave 4 sharpest opt-in. EWA Lanczos sharp is the starting
  recommended High-quality default. Evaluate current chroma Bicubic and
  downscaler Hermite against the available alternatives before settling their
  practical defaults. Preserve explicit user choices and intentional Fast/
  Balanced quality policies; changing generic defaults is not permission to
  force High onto an explicitly selected performance preset.
- Before implementation, add a compact choice inventory/default matrix to this
  story or a linked asset: page and setting, stored tokens, display labels,
  intended quality order, actual default per applicable quality/context, cost
  rationale, and legacy migration. Explain each changed default and validate
  editor/runtime agreement. This includes non-dithering defaults.

### One Scaling page with visible image and chroma controls

- **VP Renderer -> Scaling** is the sole editor for image Upscaler, Downscaler,
  Sigmoid upscaling, and Chroma upscaler. Remove these editable fields from
  **Rendering/DTM -> Processing overrides** and any other duplicate editor.
  Rendering may keep DTM, debanding, dithering and its overall quality preset.
- Use clear **Image scaling** and **Chroma scaling** sections on Scaling. Keep
  Chroma upscaling visible in the main page, with its selected algorithm and
  brief explanation of 4:2:0/4:2:2 reconstruction and RGB applicability. It must
  be reachable by normal scrolling and keyboard navigation at supported window
  sizes/DPI, not hidden behind an unrelated profile or legacy page.
- Reconcile existing Rendering scaler overrides with canonical Scaling profiles.
  Preserve intended effective values, rules, shortcuts, inherited behavior and
  profile selection. Where both locations specify different values, account
  for the old precedence; do not silently discard the higher-precedence value
  or create duplicate selectable profiles. Reuse/fix existing migration rather
  than adding a competing ownership layer. Report unresolved mappings clearly.
  Opening/cancelling must not write; persist deliberate migration on Save/Apply.
- Confirm parser, editor, migration, active-profile status, live Apply and runtime
  all resolve the same Scaling owner. Retain legacy file compatibility where
  needed without continuing an invisible Rendering override over the visible
  Scaling choice. Keep DirectShow chroma downsampling clearly distinguished
  from VP Renderer chroma reconstruction; do not invent an unsupported chroma
  downscaler.

### Dithering and motion specifics already agreed

One Dithering selector; no enable checkbox or Advanced methods list:

| Stored token | Display label, in order |
| --- | --- |
| `blue_noise` | Blue noise (recommended) |
| `ordered_fixed` | Ordered (fixed) |
| `white_noise` | White noise |
| `off` | Off |

Auto/On/true, including mixed case, display and resolve as Blue noise in the
editor and headless runtime. Normalize to `blue_noise` when saving/applying
the affected setting; mere viewing/cancelling does not rewrite it. Old
`ordered_lut` and `error_diffusion_*` values remain readable with their old
behavior, identified only by a friendly nonselectable legacy status outside
the menu. Saving unrelated settings preserves them; explicitly choosing one
of the four video choices replaces them. They never reappear as selectable
entries after profile switching.

Motion filter labels are Oversample, Hermite, Linear, Clamped Mitchell, and
Cubic only if its real mapping is proven. Add `mitchell_clamp` consistently to
validation, UI, help and production lookup. Preserve Oversample as practical
default independently of the evidence-based list order. Represent no mixing
with Smooth motion Off, without a duplicate None filter.

First reproduce the Cubic mismatch through production code. Prefer a true
temporal Cubic mapping supported by both runtimes, with kernel/radius/output
tests. Otherwise retain legacy configs without presenting a nonfunctional
choice as supported, document their effective behavior, and record migration.
No offered enabled mixer may silently resolve to null or another algorithm.

Deinterlacing uses Interlaced input only, Off, Force, with algorithms BWDIF,
YADIF, Bob, Weave. Keep From input, Top field first, Bottom field first, Field
rate (preserve motion), Frame rate, and YADIF-only spatial-skip labels clear.
Do not change their applicability or introduce motion-compensated interpolation.

## Acceptance criteria

1. The complete setting inventory has final labels, meaningful quality order,
   practical defaults and legacy handling. All affected setting menus have no
   generic Auto/default/inherited pseudo-choice or raw enum labels. Verify
   actual selections, not just text removal; the recommended default can be a
   non-first option, including the EWA Lanczos 4 sharpest example.
2. Default/named profiles, changed quality presets, inherited parent changes,
   explicit same-as-inherited selections, Reset override, local/remote editing,
   Apply/OK/reopen and Open/Cancel agree with runtime resolution. No implicit
   file writes or blanket freezing of dynamic policies. Existing explicit
   values, comments and unknown keys survive unrelated edits.
3. A verified editor build has one Scaling navigation destination and one
   editable owner per scaler key. Rendering/DTM Processing overrides contains
   no Upscaler/Downscaler/Sigmoid/Chroma controls. Chroma is visible under
   Scaling, with readable screenshots at supported sizes/DPI. Record the
   deployed/source identity difference and whether code, packaging, migration,
   or delivery caused the supplied duplicate-controls observation.
4. Test legacy Rendering scalers plus existing Scaling profiles, conflicting
   values, rules/shortcuts and subsequent profile changes. Migration preserves
   intended output and canonical ownership without losing precedence, creating
   ghost profiles, or leaving a hidden override. Check Save/Apply/reopen.
5. Verify all offered chroma algorithms reach the actual `plane_upscaler` on
   both libraries. Exercise 4:2:0, 4:2:2, native image resolution and RGB
   applicability. The UI must identify the correct profile's setting rather
   than echo an unrelated active/live profile.
6. Dithering offers exactly the four agreed choices. Auto/On/true -> Blue noise
   works on both runtimes and all quality presets, including old Auto + Fast.
   All 14 old methods remain readable; removed methods stay nonselectable.
   Off clears ordinary dithering and error diffusion. Preserve target depth
   and calibration-LUT order.
7. Production-path mixer tests reproduce old Cubic lookup failure, prove each
   offered filter's actual mapping and nontrivial temporal weights/output,
   and cover buffering/radius, Clamped Mitchell and Cubic compatibility on both
   runtimes. Deinterlacing retains all four algorithms and correct mode,
   metadata/field-order/cadence behavior and YADIF-only control enablement.
8. Validate quality/default choices with primary documentation and representative
   VP output/performance evidence; include relevant resolution, refresh rate,
   GPU and frame-budget headroom. Do not invent exact cost multipliers or
   claim a universal visual ranking from filter names alone.
9. Update configuration help/reference, UI screenshots and public-value catalogs
   to distinguish selectable choices from legacy file spellings. Run relevant
   ConfigEditorWindow, ConfigEditorCore/config-validation, renderer-profile,
   LibplaceboRenderParameters and MotionRenderer regression coverage and a
   successful x64 Release build. Record untested interactive/GPU coverage.

## Boundaries and source touchpoints

This remains one Backlog story covering shared configuration presentation,
quality/default review and scaling ownership, plus the discovered mixer gap.
New default recommendations are in scope, backed by the matrix and evidence;
explicit existing operator choices remain protected. No libplacebo upgrade,
new dithering algorithm, new capture/deinterlacing policy, RIFE, deployment or
active-config replacement is authorized. Preserve VP-0032/VP-0033 review states.

Primary source at the researched commit:

- `src/VideoProcessor-Config/ConfigEditorWindow.cpp`: profile migration (~2078),
  profile family filtering (~2772), default resolution/status (~3013), navigation
  (~4290), addChoice (~5301), Rendering (~6627), dithering (~6780), Motion
  (~6844), Scaling/chroma (~6890), profile-load defaults (~7759).
- `src/VideoProcessor-Lib/RendererProfileConfig.h`: owned-value validation and
  canonical/legacy precedence; `src/VideoProcessor-ConfigCore/ConfigEditorCore.cpp`.
- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`: choice parsing,
  profile merging and application; `LibplaceboRenderParameters.cpp`: quality,
  dithering, image and chroma parameter projection; `TemporalRenderer.h`: mixer
  lookup, buffering and production submission.
- Both `3rdparty/libplacebo*/source/` archives and matching installed headers;
  `docs/configuration-public-values.tsv`, `CONFIGURATION.html`, existing editor,
  profile/configuration, render-parameter and motion tests.

Tracker audit before original assignment: 233 canonical records and table rows,
maximum root VP-0214, no duplicate/missing IDs, child gaps or registry conflicts.
Existing unrelated discrepancies: VP-0168 index casing, VP-0070 missing expected
Status heading, and 209 historical non-exact status lines. Preserve/defer these
to dedicated tracker maintenance rather than silently changing them in VP-0215.
