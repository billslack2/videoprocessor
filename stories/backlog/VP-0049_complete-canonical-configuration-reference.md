# VP-0049: Complete canonical `CONFIGURATION.html` reference

## Status

Backlog and ready for implementation. VP-0045 is complete, so the canonical
`vpvr.*` section names and ownership boundaries are now final. No
implementation has started.

## User story

As a VideoProcessor user configuring capture, shaders, profiles, and the
built-in renderer, I want `CONFIGURATION.html` to explain every supported
public field and every available value in plain language, so I can construct a
correct configuration without reading source code, guessing what `AUTO` means,
or learning through failed playback.

## Problem

The current help compresses many unrelated settings into broad summary rows.
For example, the built-in renderer base-settings table lists field names and
enum tokens but generally does not explain:

- what each field controls;
- what stage of the pipeline it affects;
- what each enum option actually does;
- the default when omitted;
- the exact behavior of `AUTO`;
- valid ranges, units, or syntax;
- which settings are live and which require renderer reconstruction;
- interactions, precedence, and mutually exclusive combinations;
- whether a setting applies to all VP operation or only the built-in renderer;
- failure behavior and useful diagnostics; or
- practical examples and when a user should select an option.

This is insufficient as a configuration reference and makes the concise sample
configuration carry too much explanatory burden.

## Public-documentation boundary

`CONFIGURATION.html` documents only the current canonical, supported, public
configuration surface after VP-0045.

It must not mention or expose:

- backward-compatibility behavior;
- legacy section or key names;
- deprecated aliases or migration syntax;
- internal-only or deliberately hidden configuration switches;
- abandoned experimental syntax; or
- implementation details that users cannot configure.

Compatibility may continue to exist in code, tests, or developer diagnostics,
but it is outside this user-facing help file. The document must never tell a
new user to use obsolete syntax.

Before writing, establish a reviewed public-field allowlist from the canonical
schema and current supported behavior. A field that is intentionally hidden is
excluded from both the reference and public examples. A public field cannot be
omitted merely because it is uncommon or diagnostic.

## Documentation design

### 1. Information architecture

Reorganize the page into a task-oriented guide followed by a complete
reference:

1. Overview and configuration-file location
2. Minimal working configuration
3. Syntax, comments, booleans, numbers, lists, paths, and duplicate handling
4. Shared VP settings
5. Queue and timing settings
6. HDR/LLDV and conversion settings
7. Shader configuration and expressions
8. Shared profile groups and profile selection
9. Built-in renderer (`vpvr`) settings
10. Built-in renderer display behavior and refresh policy
11. Shortcuts and event actions, if public
12. Diagnostics and troubleshooting
13. Complete field index

Shared sections must appear before the `vpvr` section, matching VP-0045's
canonical configuration organization.

Provide a persistent table of contents with stable anchors so users can link
directly to a section or field.

### 2. One authoritative entry per field

Every public field gets its own reference entry. Each entry must contain, where
applicable:

- canonical section and key;
- short purpose statement;
- data type and accepted syntax;
- required/optional status;
- default behavior when omitted;
- allowed range, units, and precision;
- every accepted enum value;
- exact `AUTO` behavior;
- evaluation/read time: startup, renderer construction, source transition,
  profile selection, or live update;
- scope: shared VP, DirectShow-only, shader-only, or `vpvr`-only;
- precedence and interaction with profile/rule overrides;
- whether changing it requires renderer restart/rebuild;
- validation and failure behavior;
- concise example; and
- related fields and diagnostics.

A compact summary table may remain for navigation, but it cannot substitute for
the individual field entries.

### 3. Explain every option

Do not present an unexplained comma-separated enum list. For each allowed
option, explain:

- what it selects;
- when it is useful;
- meaningful quality/performance/compatibility tradeoffs;
- relevant prerequisites; and
- its fallback behavior when unavailable.

For algorithms such as tone mapping, gamut mapping, scalers, debanding,
dithering, presentation mode, output range/gamma/primaries, and shader stages,
describe the observable result rather than merely repeating the enum name.

### 3a. `vpvr.display` base-settings field guide

Replace the current three-column summary row for each broad area with a
dedicated subsection that a user can read independently. This is specifically
required for every public key in `[vpvr.display]`; the renderer-session policy
keys in `[vpvr.general]` receive the same treatment in their own subsection.

The guide must include, at minimum:

- **Input treatment:** `tone_mapping`, `gamut_mapping`, `peak_detection`,
  `contrast_recovery`, and `sdr_input_transfer`.
- **Scaling:** `quality`, `upscaler`, `downscaler`, `deband`, and `dithering`.
- **Display target:** `sdr_target_nits`, `sdr_black_nits`,
  `output_presentation`, `output_range`, `output_gamma`,
  `sdr_target_primaries`, and `report_bt2020_to_display`.

For example, the Scaling subsection must give each of its five fields a
separate entry. Its `upscaler` and `downscaler` entries must then explain each
accepted algorithm token individually, including `AUTO`, rather than listing
the tokens in a table cell. `quality`, `deband`, and `dithering` require the
same per-option treatment.

Every `[vpvr.display]` field entry must visibly state:

1. what image-processing stage or output contract it controls;
2. accepted type, range/units, default, and exact omission behavior;
3. every accepted option, including its quality/performance/compatibility
   tradeoff and fallback when unavailable;
4. field-specific `AUTO` resolution and diagnostics, where supported;
5. interaction and precedence with the relevant profile group; and
6. a small canonical configuration example.

Use stable field anchors such as `#vpvr-display-upscaler` so the summary table
can link to each detailed entry. The summary table may remain as a compact
index, but it must link to—not replace—the field guide.

### 4. Define `AUTO` precisely

`AUTO` must never be documented only as "automatic" or "recommended." For
every field that accepts it, document the actual decision process:

1. what runtime evidence is inspected;
2. which component makes the decision;
3. the precedence/fallback order;
4. what happens when evidence or platform support is unavailable;
5. whether the result can change after startup or source transition; and
6. how the chosen effective value is reported in logs or the OSD.

If different fields implement different meanings of `AUTO`, document them
independently. Do not imply one global `AUTO` policy.

### 5. Explain ownership and precedence

Clearly distinguish:

- settings applied by VP itself;
- shared profile state consumed by both renderers;
- DirectShow/madVR-facing settings;
- shader configuration; and
- `vpvr.*` settings owned only by the built-in renderer.

Include a simple precedence model showing how base values, automatic profiles,
manual key selections, display rules, per-rule overrides, source metadata, and
runtime defaults combine. For omitted profile values, explicitly state whether
the base value is inherited or a separate default is used.

The page must not suggest that a `vpvr.*` setting configures madVR.

### 6. Examples

Provide small, valid examples for common tasks rather than one oversized sample:

- minimal VP capture with madVR;
- minimal capture with the built-in renderer;
- SDR Rec.709 built-in-renderer output;
- SDR BT.2020 output and reporting;
- HDR/LLDV input handling;
- 16:9 and scope/CIH viewport profiles;
- shader selection by signal/rate;
- display refresh-rate override;
- display-rule selection and manual/automatic profile keys; and
- optional event actions if they remain public.

Examples must use only canonical public syntax and must not contain hidden,
deprecated, compatibility, or placeholder settings that VP rejects.

### 7. Troubleshooting

Add symptom-oriented guidance tied to exact diagnostics, including:

- a section/key being ignored or rejected;
- a renderer-only setting used with another renderer;
- `AUTO` resolving differently than expected;
- invalid range, enum, ratio, path, expression, or duplicate key;
- missing shader/LUT file;
- refresh switching not occurring;
- incorrect output range/primaries/gamma;
- profile rule not matching; and
- setting requiring a renderer rebuild.

Reference stable log phrases only when they are part of the supported
diagnostic contract.

## Canonical field inventory

Before editing prose, derive a machine-checkable inventory from:

- the canonical configuration schema after VP-0045;
- public parser/loader registrations;
- current checked-in sample configuration;
- renderer profile and expression schemas;
- shader parameter definitions;
- public shortcuts/event-action schema; and
- documented allowed values and defaults in implementation tests.

For every inventory item, record:

- public or hidden classification;
- owning component;
- section/key;
- type/default/range/options;
- whether `AUTO` is accepted;
- relevant source/test that establishes behavior; and
- final help anchor.

Resolve discrepancies in code/schema/tests before documenting them. Do not
invent behavior to fill a documentation gap.

## Presentation and accessibility

- Use readable headings and field-name anchors instead of dense walls of text.
- Keep tables narrow enough to work at typical desktop browser widths.
- Use definition lists or field cards where a wide table would become
  unreadable.
- Ensure code samples wrap or scroll safely.
- Maintain sufficient contrast and visible keyboard focus.
- Make search terms include both user-facing concepts and exact key names.
- Avoid unexplained abbreviations; define HDR, SDR, EOTF, CIH, LUT, NLS, PPM,
  and relevant color-space terms on first use or in a glossary.

## Verification

1. Add a documentation completeness test that compares the canonical public
   field inventory with stable field anchors in `CONFIGURATION.html`.
2. Fail validation when a public canonical field has no reference entry or
   when the page documents a non-public/unknown field as configurable.
3. Validate every shown INI example through the production parser/schema.
4. Test every documented enum token and range boundary against implementation
   tests or a generated schema fixture.
5. Verify each documented `AUTO` field has a non-empty, field-specific behavior
   explanation—not merely the word "automatic."
6. Search the final HTML for all legacy, deprecated, compatibility, and hidden
   names; the public page must contain none.
7. Verify all internal anchors and links, document structure, HTML validity,
   keyboard navigation, and rendering at common desktop widths.
8. Have a reviewer unfamiliar with the implementation configure at least:
   - basic madVR output;
   - basic built-in-renderer SDR output;
   - a scope/CIH viewport; and
   - one conditional shader rule
   using only the help file.
9. Build the x64 Release configuration and confirm the shipped help artifact
   is the reviewed canonical file.

## Acceptance criteria

- Every canonical public configuration field has a clear individual reference
  entry.
- Every accepted enum option is explained, not merely listed.
- Every accepted `AUTO` value has precise, field-specific resolution and
  fallback behavior.
- Defaults, units, ranges, ownership, precedence, lifecycle/restart behavior,
  validation, and examples are documented wherever applicable.
- Shared sections appear before the `vpvr` renderer section.
- `vpvr.*` ownership is unmistakable and is never described as configuring
  madVR.
- No backward-compatibility, legacy, deprecated, migration, hidden, or rejected
  configuration syntax appears in the user-facing help.
- All examples parse successfully and match current supported behavior.
- Automated completeness checks prevent new public fields from being added
  without documentation.
- The final page is useful as both a beginner guide and a precise field
  reference.

## Dependencies

Blocked on completion of VP-0045 so the canonical public namespace and section
ordering are stable before the rewrite begins.
