# VP-0128: Audit VP Renderer option parity and resolved defaults

## Status

Done (2026-09-20). The user authorized deployment and final check-in.
[PR #106](https://github.com/billslack2/videoprocessor/pull/106) merged into
`v1.3.005-beta` as `38477f13560a3c4f0085c8840127ae064d0a5b86`.
The merged tree is identical to tested/deployed source
`90fd0ed8b1943710348bbe01cdf69c28d1f857da` (`git diff --exit-code` passed).
Included zero-black PR #105 is also recorded as merged by GitHub.

Validation: clean x64 Release build, 80 editor tests, and 440 relevant native
tests passed. Deployed host/renderer/editor/discovery hashes were verified;
the subtitle diagnostic setting was removed as requested, with the original
config and all replaced files backed up. The deployment receipt is linked below.
Physical HDMI/capture and second-monitor qualification were not performed;
this completion records the configuration audit/fixes and authorized check-in,
not a claim of new physical-output qualification. No unrelated subtitle work
was merged or resumed.

## Implementation and deployment history

Review (2026-09-20). The confirmed audit corrections are implemented and tested
in [draft PR #106](https://github.com/billslack2/videoprocessor/pull/106), including
the zero-black work previously reviewed separately in PR #105. Current source
head: `90fd0ed8b1943710348bbe01cdf69c28d1f857da`; branch
`codex/vp-0128-config-corrections`; worktree
`E:\codex\videoprocessor\vp-0128-config-corrections`.

Latest GitHub default/beta was rechecked before publication and remains
`v1.3.005-beta` at `b3c0b3a6a8fdf4f5bb1c9ce0dc3340a3d5e395d7`.
[Correction scope and validation](https://github.com/billslack2/videoprocessor/blob/90fd0ed8b1943710348bbe01cdf69c28d1f857da/docs/VP-0128-config-corrections.md).

All confirmed F1-F9 findings are addressed: inherited Boolean/effective-value
initialization, strict invalid-token preservation, DirectShow Auto/omission at
90 ms, unambiguous Base section identity and explicit collision rejection,
zero default black with a 0.000001-nit native floor, flag-derived diagnostic
preset labels, and aligned reference inventories/resolved logs. Additional
cases fix logging-retention fallback, PPM Auto's numeric sentinel collision,
and omitted screen-aspect presentation. Existing explicit calibration values
are preserved; legacy black AUTO intentionally adopts the approved zero policy.

Combined validation: clean x64 Release solution rebuild passed (0 errors,
48 warnings); all 80 editor tests and 440 relevant native tests passed, including
reference/inventory/example checks. An incremental LNK1103 corrupt debug-info
failure cleared with a clean rebuild without source changes. The editor suite
covers synthetic monitor placement; a second physical monitor and HDMI/capture
behavior remain unqualified. Local logs are under
`artifacts/vp0128-corrections/` in the source worktree.

Ready for code/build review and a subsequent beta merge decision. The user
authorized deployment, now completed as recorded below. Acceptance criteria
have automated/source evidence recorded in the linked report; final acceptance
and physical output qualification remain pending. The historical findings and
intermediate results below are superseded by this combined correction status.

## Authorized deployment — 2026-09-20 22:04 EDT

Deployed the tested VP-0128 source `90fd0ed8b1943710348bbe01cdf69c28d1f857da`
to `C:\Videoprocessor\vp` using its successful x64 Release build. Replaced and
SHA-256 verified the host, paired renderer DLL, Config executable, discovery DLL,
and configuration reference against build/source files. Runtime records confirm
Release/x64 identity; installed Qt Core/Gui/Widgets match the build dependencies.
The background Config process restarted successfully. Playback was not launched.

At the user's explicit instruction this overwrites the installed VP-0070 trial;
no combined-trial deployment was used. Removed `subtitle_bbox_test: true`, its
VP-0070 diagnostic comment and the now-empty final `[vprenderer]` trial section.
All remaining config bytes were preserved. Original config and every replaced
file are backed up under
`C:\Videoprocessor\vp\backup-before-vp0128-20260920-220425`.
[Deployment receipt and file hashes](../assets/VP-0128/deployment-90fd0ed8/deployment-receipt.json).

PR #106 remains unmerged. Story stays Review pending acceptance/live playback;
this deployment does not claim physical capture/HDMI qualification.

## Pinned audit history — 2026-09-20

In Progress (2026-09-20). The comprehensive audit is complete; corrective
work remains open; the requested F7 zero-black correction is implemented for
review in PR #105 (details below). Current beta baseline was queried
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
The audit pass made no production behavior changes, deployment, merge or
active-config edits. The subsequent F7 source correction is recorded below.

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

Historical proposal, superseded by the user-approved zero default below:
for Auto black, retain an Auto/explicit discriminator until inherited target
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
## Zero-black correction implemented for review — 2026-09-20

The user clarified that default black should be zero, which VP internally maps
to almost zero. Latest beta was rechecked and remained `v1.3.005-beta` at
`b3c0b3a6a8fdf4f5bb1c9ce0dc3340a3d5e395d7`. Fresh profiles already used zero;
omission and legacy AUTO still used white/1000. This correction makes both use
zero, resolving F7 by the revised policy rather than retaining the ratio.

[Draft PR #105](https://github.com/billslack2/videoprocessor/pull/105), source
commit `a85dda6fd4621f079c6533ae7583f169a1497f14`, branch
`codex/vp-0128-zero-black-default`, worktree
`E:\codex\videoprocessor\vp-0128-zero-black-default`.
[Implementation and validation](https://github.com/billslack2/videoprocessor/blob/a85dda6fd4621f079c6533ae7583f169a1497f14/docs/VP-0128-zero-black-default.md).

Runtime initialization, omission/Auto parsing, inherited profiles, invalid-pair
fallback, editor fallback/status/help and reference examples now agree.
Zero maps to 0.000001 nit at the native boundary. Valid measured numeric black
levels continue to inherit; SDR reference luminance is unchanged. Existing
explicit AUTO configurations intentionally adopt the new zero policy without
rewriting their files.

Validation: x64 Release build passed (0 errors, 47 warnings); all 76 editor tests
passed. Native Config/Profile/Libplacebo filter: 356/357 passed initially,
including both new black regressions. The existing immediate-reload test
`ProfileChangeDisplayDurationIsBoundedAndLive` failed once and passed on an
isolated rerun without code changes. No merge, deployment or active-config edit;
physical output was not qualified. Other audit findings and overall acceptance
remain open; tracker stays In Progress.

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

## RC2 distribution — 2026-09-20

The merged fixes are included in published [1.3 RC2](https://github.com/billslack2/videoprocessor/releases/tag/1.3-beta-RC2),
tagged at `38477f13560a3c4f0085c8840127ae064d0a5b86`. Fresh installer and portable
ZIP builds passed release qualification; [evidence and hashes](../assets/releases/1.3-beta-RC2/release-verification.json).
This packaging/publication did not change the active deployment or resume subtitle work.
