# VP-0215: Make configuration choices clear and consolidate scaling

## Status

In Progress

2026-10-25 window-size follow-up: Advanced tone mapping must maximize the
editor window (ordinary maximized state, not borderless full-screen). Basic
must restore the exact prior size/position and prior maximized state. The user
clarified this before implementation. Continue the requested deployed trial
branch at 405cdea1; discovered/fetched beta remains v1.4.02-beta at 8c453746.
Preserve panel/section/scroll restoration and pending edits. Implement and test
normal/repeated/already-maximized transitions, then deploy a completed matched
x64 Release build with configuration backups and no active-file edits.

Previous trial evidence follows; it remains valid for that recorded commit.

2026-10-25: The requested follow-up is implemented, tested and deployed for user
trial. Clean source `405cdea1987a720666b3d2a87e2c1c4e0c0ff710` is pushed on
`codex/vp0215-config-choices-20261025`. It continues the user-selected local
Config trial `a37dab1f7c518de7c585dff3c479e84c70038598`, on local docking base
`eab55b4830877a8ebfb943c40a1d5dacbcf795ae` and discovered current beta
`v1.4.02-beta` at `8c4537462bbabd6ce07ba3e9ffd9be9bc8745492`. No beta merge,
PR, installer publication or public release was performed.

Completed follow-up:

- Tone Mapping replaces Rendering / DTM in the tab and page title. The entire
  Processing overrides section and its adoption button are removed.
- One Scaling section contains image and chroma controls on Scaling / Quality.
  Debanding / Dithering replaces Debanding_Dithering; Blue noise has no suffix.
- One Advanced tone mapping toggle reads Basic tone mapping while enabled.
  Returning restores prior expanded controls, docking and scroll; no separate
  Restore panels button remains.
- Shared parser/editor migration moves old Rendering processing values to
  visible Scaling profiles, preserving previous precedence and distinct named
  values. Relevant rules/shortcuts/labels transfer where absent; comments and
  legacy removed dithering methods remain readable. Load changes memory only;
  Save/Apply persists the plan with an original-file migration backup.
- Named Tone Mapping peak-detection and contrast-recovery defaults are frozen
  in the same migration. This repairs a second-migration-on-reopen regression
  found by final Save/Cancel and remote Apply tests. Explicit Auto reset semantics
  and effective inherited values are covered by the added native regression.

Validation against this exact clean commit: complete x64 Release solution
Rebuild; 141 targeted native regressions, 46 Config
cases, all three 100%/150%/200% DPI runs and the beta parameter program passed.
All five host/editor/discovery/stable/beta binary runtime receipts and deployed
hashes were verified. Config includes import/export, comment preservation,
legacy ownership, Save/Apply/Cancel/reopen, local/remote Apply, docking and the
single toggle. Earlier failed link/test logs are retained locally, and no
failed build was deployed. The pre-existing all-pages tab-size assertion is
recorded under prior trial evidence and is not counted as a pass. No whole-repo
native suite pass or physical playback/performance qualification is claimed.

Deployment: `2026-10-25T21:03:42.7020004-04:00` to `C:\Videoprocessor\vp`.
Rollback backup: `C:\Videoprocessor\vp\backup-before-vp0215-scaling-405cdea1987a-20261025-210341`.
All 7 captured active configuration/state files were
backed up and verified byte-for-byte unchanged. Configuration edits: none.
Installed Scaling and Tone Mapping read-only open/screenshot/exit checks passed,
with settings hashes unchanged. Config is reopened for the user's trial.

Remaining acceptance: user validation of revised menus, unified Scaling and
Advanced/Basic behavior; independent cross-family legacy selection combinations;
physical playback, chroma/deinterlacing end-to-end behavior on both runtimes;
representative perceived-quality and performance comparisons; exhaustive
General/input/dynamic-policy presentation review. Migration is best-effort for
old independent profile-selection cross-products. Existing Hermite downscale
and Bicubic chroma defaults remain pending measured review. Cubic stays legacy
only because enabling real Cubic failed the earlier opaque-subtitle regression.
Proposed decision: collect user feedback before merge. Keep this story, VP-0032
and VP-0033 in Review.

Follow-up evidence:

- [Updated choice/default/ownership inventory](../assets/VP-0215/followup-choice-inventory.md).
- [Build, tests and runtime identities](../assets/VP-0215/followup-validation.json).
- [Deployment and configuration preservation](../assets/VP-0215/followup-deployment.json).
- [Installed editor smoke](../assets/VP-0215/followup-deployed-smoke.json).
- [Installed Scaling](../assets/VP-0215/followup-deployed-scaling.png) and
  [installed Tone Mapping](../assets/VP-0215/followup-deployed-tone-mapping.png).
- [Unified Scaling layout](../assets/VP-0215/followup-scaling.png),
  [basic Tone Mapping](../assets/VP-0215/followup-tone-basic.png), and
  [advanced Tone Mapping](../assets/VP-0215/followup-tone-advanced.png).
- [Interactive mockup screenshot](../assets/VP-0215/followup-mockup-scaling.png).

Earlier a37dab1f trial evidence remains under `trial-*` assets. Its retained
Rendering processing controls and separate Image scaling / Chroma scaling
sections are superseded by this follow-up. Earlier research below records the
original beta baseline; intended design and acceptance reflect the latest user
instructions.

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
  quality without unusually high video-rendering cost. Select that actual option
  directly; do not add a duplicate default row or recommendation suffix. Expensive high-end choices
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

- **Scaling / Quality** is the sole editor for image Upscaler, Downscaler,
  Sigmoid upscaling, Chroma upscaler, Debanding, Dithering and dither depth.
  **Tone Mapping** has no Processing overrides section or legacy adoption button.
- Use one **Scaling** section containing image and chroma controls. Keep Chroma
  upscaler beside image scaling with a brief explanation of 4:2:0/4:2:2
  reconstruction and RGB applicability. Use **Debanding / Dithering** for the
  remaining section. All controls must remain reachable by scrolling/keyboard.
- Load existing Rendering processing values into visible Scaling profiles,
  honoring the prior higher precedence. Reuse a matching destination or create
  a named Scaling profile for a distinct legacy choice; avoid redundant copies.
  Preserve values, comments and unknown keys, and carry relevant rules/shortcuts
  when the destination lacks them. Review independently selected profile
  combinations rather than claiming a lossless mapping for every cross-product.
  Open/Cancel must not write; Save/Apply deliberately persists migration with
  an original-file backup.
- Parser, editor, migration, profile status and rendering use the same canonical
  Scaling owner. Preserve legacy file compatibility without invisible Rendering
  overrides. DirectShow chroma downsampling remains distinct from VP Renderer
  reconstruction; do not invent a chroma downscaler.
- Rename Rendering / DTM to **Tone Mapping**. Provide one **Advanced tone
  mapping** toggle that reads **Basic tone mapping** while enabled. It must
  restore the prior basic layout without a separate Restore panels button.

### Dithering and motion specifics already agreed

One Dithering selector; no enable checkbox or Advanced methods list:

| Stored token | Display label, in order |
| --- | --- |
| `blue_noise` | Blue noise |
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
   editable owner per scaler key. Tone Mapping has no
   Processing overrides section. Image and chroma share one Scaling section,
   with Debanding / Dithering below it, with readable screenshots at supported sizes/DPI. Record the
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
10. Tone Mapping uses one Advanced/Basic toggle; returning to Basic restores
    expanded controls and prior workspace layout. Blue noise has no suffix.

## Boundaries and source touchpoints

This remains one story covering shared configuration presentation,
quality/default review and scaling ownership, plus the discovered mixer gap.
New default recommendations are in scope, backed by the matrix and evidence;
explicit existing operator choices remain protected. No libplacebo upgrade,
new dithering algorithm, new capture/deinterlacing policy, RIFE, or active-config
replacement is authorized. The later user request explicitly authorized the trial
binary deployment described above. Preserve VP-0032/VP-0033 review states.

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
