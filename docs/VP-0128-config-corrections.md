# VP-0128: confirmed configuration corrections

Baseline: GitHub default/current beta `v1.3.005-beta`,
`b3c0b3a6a8fdf4f5bb1c9ce0dc3340a3d5e395d7`, queried and fetched 2026-09-20.
Source branch: `codex/vp-0128-config-corrections`.
This branch includes the zero-black correction from PR #105, so the full audit
correction can be reviewed and integrated together.

## Findings addressed

- F1: profile checkboxes use the same true/false alias rules as other editor
  fields and runtime parsing. Inherited aliases load correctly and untouched
  spellings survive saving.
- F2/F3: Auto status uses effective inherited quality/range, including Fast,
  Balanced and Limited transport. Compatibility notices refresh when profiles
  load, including initial page construction.
- F4: only retired `none` and `ewa_lanczos` downscalers migrate to explicit Auto.
  Keeping Auto explicit prevents accidentally inheriting a parent's named
  algorithm. Unknown filters and malformed strict fixed-crop aspects remain
  visible and block Apply with an actionable error. Forgiving optional crop
  limits retain their existing contract.
- F5: DirectShow omission and Auto use the user-approved 90-ms default. Startup,
  explicit Auto command-line overrides, editor presentation and live apply agree;
  explicit numeric offsets (including zero) remain supported. Auto for the built-in
  renderer remains zero and does not overwrite the preserved DirectShow value.
- F6: one shared resolver uses actual root presence to distinguish the synthetic
  root from a user-named Base. Renderer dispatch, active status and shader section
  selection use it. Root-plus-Base collisions are rejected in the parser/editor.
  The editor does not hide such collisions by automatically renaming the root.
  Unambiguous existing Base names and persisted names remain unchanged.
- F7: omitted and legacy Auto HDR target black now select zero, mapped to
  0.000001 nit at the native boundary. Existing numeric measured black values
  and inheritance remain supported. The editor presents the numeric default.
- F8: diagnostic preset presentation is derived from effective flags on load and
  edit. A saved preset name alone never enables experiments. Selecting a preset
  still writes its five diagnostic flags; ordinary output/calibration is separate.
- F9: canonical Color/Scaling/Zoom ownership, missing settings, accepted algorithm
  names, Auto help, transfer normalization and reference examples are aligned.
  Native logs distinguish Standard/HQ peak detection and actual dithering kernels.
  Serialized frame-mixer pointers are explicitly identified as inactive because
  VP renders individual images.
- Additional audit cases: invalid logging retention displays runtime fallback 10;
  PPM Auto uses an independent mode instead of the valid number 999999; omitted
  screen aspect displays the output-aspect fallback rather than inventing 16:9.

## Boundaries

This corrects configuration behavior and representation, not calibration or
performance tuning. Existing explicit calibration, queue, quality and transport
values are retained. Legacy file-only overlapping processing keys remain accepted;
new controls use their canonical owners. Scaling previews describe the Rendering
profile currently displayed in the editor, not a different active session profile.

No beta merge, deployment, active-config replacement or physical capture/HDMI
qualification occurred. The local `C:\logs\vp.log` was unavailable. Physical
second-monitor editor placement is skipped on the single-monitor test host;
synthetic placement coverage remains in the suite.

## Validation

- Combined clean x64 Release solution rebuild passed: zero errors, 48 warnings.
  The preceding incremental link hit LNK1103 corrupt debug information; the clean
  rebuild regenerated the cached objects and resolved it without source changes.
- Full combined editor suite: 80 passed, exit 0.
- Combined relevant native suite: 440 passed, exit 0. Covers configuration,
  profiles, libplacebo projection, queue/startup, LLDV and shaders, including
  policy, identity, PPM, zero-black and resolved-diagnostics regressions.
- Reference/inventory/example validation is included in the passing native suite.
- `git diff --check` passed. Initial regression failures exposed and led to fixes
  for pre-attachment widget lookup and implicit root renaming; the native kernel
  assertion was corrected to the bundled library's actual `sierra-3` name.
- Logs are local under `artifacts/vp0128-corrections/`:
  `build-combined-clean.log`, `editor-combined.log`, `native-combined.log`, and
  `config-corrections-combined.trx`. Earlier individual-branch logs are retained.

PR #105 is included in this branch. One documentation conflict was resolved by
keeping the zero-black contract together with the corrected gamma/range guidance.
The combined source passed the build and tests above before publication.
