# VP-0128: Audit VP Renderer option parity and resolved defaults

## Status

Review (2026-08-30). The dithering parity increment is implemented in
`billslack2/videoprocessor` commit `d6e7fab2` on `v1.3.004-beta`: the editor,
profile validation, runtime mapping, and diagnostics now expose Auto, the four
ordinary libplacebo dithering methods, all ten error-diffusion kernels, and
Off. Legacy `on` continues to resolve to Blue noise. Error diffusion is
intentionally disabled while a display 3D LUT is active; this is a compatible
render-path constraint, not a LUT requirement for dithering. The targeted
native mapping test and x64 Release renderer build passed; the deployed
configuration editor is ready for reviewer validation.

The remaining audit scope covers the complete VP Renderer configuration surface
before changing behavior. The immediate confirmed inconsistency is that
`quality: high` plus `downscaler: AUTO` resolves to libplacebo's Hermite
downscaler, while VP's configuration UI and parser expose only `AUTO`, EWA
Lanczos, Bicubic, and Bilinear. Hermite cannot be named or selected explicitly.
The Balanced preset similarly inherits plain Lanczos upscaling even though the
explicit upscaler choices expose EWA Lanczos variants rather than plain
Lanczos. This story is an audit first; it must not turn every libplacebo field
into a user-facing control without a deliberate product decision.

## User story

As a VP Renderer user, I need every configuration choice and inherited default
to use truthful, consistent names across the UI, saved configuration, renderer,
logs, and documentation, so I can understand whether I am retaining a quality
preset or explicitly selecting a different algorithm.

## Confirmed problem

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
