# VP-0128: Audit VP Renderer option parity and resolved defaults

## Status

In Progress (2026-09-20). The comprehensive audit is complete; corrective
implementation and acceptance remain open. Current beta baseline was queried
from GitHub: `v1.3.005-beta` / `b3c0b3a6a8fdf4f5bb1c9ce0dc3340a3d5e395d7`.
Audit and reproducible probes are committed and pushed as `787a3e5ffd863f48a7ee70cdcdb5b6109b989302`
on `codex/vp-0128-config-audit-20260920`, in worktree
`E:\codex\videoprocessor\vp-0128-config-audit-20260920`.

[Full audit, field/default matrices and evidence](https://github.com/billslack2/videoprocessor/blob/787a3e5ffd863f48a7ee70cdcdb5b6109b989302/docs/VP-0128-configuration-audit.md).

Executed probes confirm valid Boolean aliases displayed unchecked, inherited
Fast/Balanced quality described as High, inherited Limited transport described
as unused, and arbitrary unknown downscalers silently removed on Save. Source
traces identify additional corrections: DirectShow Auto still calculates from
queue/rate despite the user's clarified intended 90-ms policy; named Base
profiles collide with the synthetic root; Auto black is resolved before final
inherited white; diagnostic preset
tokens are editor metadata rather than runtime presets. Documentation and
summary logging also need alignment. The report separates executable evidence
from source-traced findings and lists smaller follow-up cases.

The original missing Hermite/Lanczos choices have since been implemented, and
Dithering Off clears both native paths. The historical LUT/error-diffusion
exclusion no longer applies: target-frame calibration LUTs run before final
dithering. Earlier descriptions below are historical examples, superseded by
the pinned audit where they disagree.

Validation: x64 Release solution build passed (0 errors); all 75 editor tests
and 435 relevant native tests passed. Additional characterization probes
reproduced the gaps missed by those suites. The bundled libplacebo 7.360.1/API
360 preset matrix was read through production mapping code from the hashed DLL.
Physical HDMI/capture behavior and a second physical monitor were not qualified.
No production behavior changes, deployment, merge or active-config edits.

Next increments, in order: shared effective-value/Boolean UI resolution;
strict unknown-token preservation; explicit source-section identity and
resolve-after-inheritance defaults; shared startup defaults; then truthful
preset/help/log/reference generation. Keep Auto for real quality, timing,
conversion and presentation policies; do not present display calibration as
automatically detected. Existing compatibility defaults must not be silently
retuned. Acceptance criteria 2/4/7/8/9 remain open pending corrections and
focused regressions.

## Follow-up clarification — 2026-09-20

Rechecked GitHub: latest/default beta and its commit remain unchanged. The user
confirmed that DirectShow Auto should mean 90 ms. The omitted fixed 90-ms value
is a sensible intended default; align explicit Auto, startup, live apply and
help with that policy while preserving explicit numbers and the built-in
renderer's separate neutral timestamp handling. This is a correction to the
audit's framing, not evidence that the current Auto resolver already returns 90.

For Auto black, retain an Auto/explicit discriminator until inherited target
white is final: white 100 -> 200 must change Auto black 0.100 -> 0.200, while
explicit zero/measured black stays unchanged. For Base, carry actual section
identity across renderer, GUI selection and active status; support unambiguous
existing names and explicitly reject root-plus-named-Base identity collisions
in parser/editor. Do not silently ignore or rename existing profiles.

The audit now contains a concrete per-finding correction/regression table.
Boolean/effective-value UI handling and strict invalid-token preservation are
small localized fixes; Auto black/default resolution and profile identity need
coordinated tests. Prefer deriving diagnostic preset labels from actual flags.
No implementation or deployment occurred in this clarification pass.
## User story

As a VP Renderer user, I need every configuration choice and inherited default
to use truthful, consistent names across the UI, saved configuration, renderer,
logs, and documentation, so I can understand whether I am retaining a quality
preset or explicitly selecting a different algorithm.

## Original confirmed problem (historical)

VP currently presents three different concepts without making their
relationship clear:

1. **Inherited / not set** removes the selected profile's key and may inherit
   another profile value.
2. **Auto** writes an explicit `AUTO` token and retains the selected libplacebo
   quality preset's value.
3. Named choices override one field of that preset.

The named choices are not a complete list of the algorithms that Auto can
resolve to. In particular:

- High Auto downscaling is Hermite, but neither the UI list nor parser accepts
  an explicit `hermite` token.
- Balanced Auto upscaling is plain Lanczos, but the explicit UI/parser choices
  offer EWA LanczosSharp, EWA Lanczos, Bicubic, and Bilinear rather than plain
  Lanczos.
- The Debanding label **Standard** saves the token `default`; that alias may be
  reasonable, but the UI does not explain that it means libplacebo's default
  strength rather than the selected quality preset.
- Auto labels do not show their resolved Fast/Balanced/High value, so a user
  cannot discover that Auto means Hermite, plain Lanczos, enabled sigmoid, or
  another preset-owned choice without consulting external documentation.

These are confirmed examples, not an exhaustive defect list.

## Required audit

Build one version-pinned matrix for every VP Renderer field and every profile
override that records:

- UI label and ordering;
- combo-box data/saved token;
- profile inheritance behavior;
- parser-accepted values and validation/fallback behavior;
- internal libplacebo export or parameter selected by each token;
- effective Fast, Balanced, and High preset values when Auto is used;
- effective startup log/diagnostic wording; and
- `CONFIGURATION.html` syntax and behavioral description.

At minimum, audit quality, upscaler, downscaler, debanding strength, sigmoid,
dithering and error diffusion, frame mixing, tone mapping, gamut mapping, peak
detection, contrast recovery, output presentation/range/gamma, SDR input
transfer, target primaries, target/black luminance, LUT controls, and all
Output Experiments fields. Check coupled state as well as individual pointers:
for example, an explicit Dithering Off must not leave a preset-owned error
diffusion path active while the UI reports dithering as disabled.

Classify every mismatch as one of:

1. a supported runtime value missing from the UI;
2. a preset-only implementation detail that should remain Auto-only but must
   be named in the resolved-value help/status;
3. a UI value the parser cannot accept or does not apply as labelled;
4. a label/token alias that needs clearer wording rather than a new value;
5. a documentation or effective-logging defect; or
6. an intentionally unsupported libplacebo capability that should remain
   unexposed.

## Candidate UI direction

For preset-owned controls, show the current resolution without pretending Auto
is an adaptive algorithm search. Examples:

- `Auto (High preset: Hermite)` for downscaling;
- `Auto (Balanced preset: Lanczos)` for upscaling;
- `Auto (High preset: On, standard strength)` for debanding; and
- `Inherited: Auto (High preset: Hermite)` where a named profile inherits the
  default profile's explicit Auto value.

If Hermite or plain Lanczos are approved as explicit overrides, add the token
end to end: UI data, parser allow-list, libplacebo export mapping, saved-config
round trip, effective log, reference documentation, and focused tests. Do not
add a menu label that the renderer cannot consume, and do not silently map one
algorithm name to another.

## Acceptance criteria

1. The audit matrix covers every current VP Renderer and Output Experiments
   field and identifies every UI/parser/runtime/log/documentation mismatch.
2. Every displayed named choice saves a parser-accepted token and selects the
   algorithm or behavior named by the UI.
3. Every supported explicit runtime value intended for users is selectable;
   intentionally preset-only or unsupported values are documented as such.
4. Inherited, Auto, and explicit override states are visually distinct, and
   Auto exposes its resolved value for the selected quality preset.
5. High Auto downscaling is visibly identified as Hermite. If explicit Hermite
   is approved, `hermite` round-trips and selects `pl_filter_hermite`.
6. Balanced Auto upscaling is visibly identified as plain Lanczos. If explicit
   Lanczos is approved, its token is distinct from EWA Lanczos and selects the
   correct libplacebo export.
7. Off/On controls are audited for all coupled preset state, including
   dithering versus error diffusion, so effective behavior matches the label.
8. Existing configuration remains backward compatible; aliases are migrated
   only through an explicit, tested rule and unknown tokens remain actionable
   validation errors rather than silent substitutions.
9. Focused configuration-editor and renderer-parameter tests prove choice-list
   completeness, token round trips, profile inheritance, all three quality
   presets, effective logs, and documented examples.
10. A clean x64 Release build and the relevant native/configuration test suites
    pass before deployment. Renderer behavior outside approved option-parity
    corrections remains unchanged.

## Boundaries and dependencies

- VP-0086 and VP-0049 own the configuration reference and must remain aligned
  with the audited matrix.
- VP-0097 owns the standalone editor's persistence and safety contract.
- VP-0102 and VP-0113 own broader Modern UI structure and layout; this story
  changes option truthfulness, not general visual design.
- VP-0125 owns the experimental VP-owned output path. Its diagnostic controls
  are included in the audit, but this story must not alter presentation
  architecture or attempt to repair the independent stale-frame finding.
- Pin conclusions to the bundled libplacebo version. A library upgrade requires
  regenerating and reviewing the preset-resolution matrix.
