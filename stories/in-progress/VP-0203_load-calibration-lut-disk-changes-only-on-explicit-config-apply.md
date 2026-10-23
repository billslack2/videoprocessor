# VP-0203: Load calibration LUT disk changes only on explicit Config Apply

## Status

In Progress

Implementation starts 2026-10-22 on `codex/lut-explicit-apply-20261022` in
`E:\codex\videoprocessor\lut-explicit-apply-20261022`, from verified remote
beta `v1.4.02-beta` at `fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`.
Readiness review below is complete. No implementation commit or validation result
is claimed yet; the next action is the narrow Apply-only reload change and its
same-path regression coverage.

Created 2026-10-22 from the authorized render-thread stall review follow-up.
Implementation is authorized; deployment, player restart, and changes to active
user configuration or LUT files are not authorized by this story.

## User story

As a VP user, I want edits to calibration LUT files to take effect when I use
Config Apply or OK, so playback does not poll the filesystem or unexpectedly
parse and replace calibration data while rendering frames.

## Scope and behavior

- Remove automatic calibration-LUT filesystem polling and same-path hot reload
  from the playback/render loop.
- Editing, replacing, deleting, or temporarily making a selected LUT unreadable
  on disk has no effect on the active renderer until explicit Config Apply/OK.
- An accepted explicit Apply/OK must attempt to reload the selected LUT even
  when its path, configuration values, and file size/timestamp are unchanged.
- Parse and validate the candidate before replacing the active LUT. A rejected
  same-path reload under the same calibration contract retains the valid active
  LUT and reports the rejection. Preserve safe existing disable/slot/target and
  incompatible-contract handling; do not apply old bytes under a new contract.
- Startup and an actual configured LUT/target selection still acquire the
  requested resource. This change governs in-place disk edits, not LUT color
  semantics, renderer resource ownership, or unrelated configuration behavior.
- Update configuration help/reference text that currently promises automatic
  same-path LUT detection. Add focused diagnostics for explicit reload outcome
  without introducing per-frame logging.

## Predecessors and boundaries

- [VP-0166](../done/VP-0166_correct-integrated-target-frame-3D-LUT-application.md)
  is accepted historical LUT support. This story supersedes only its requirement
  to check selected-file size/write identity once per second. It does not reopen
  VP-0166 or change the post-DTM calibration order, gamut slot selection, path
  constraints, Cube validation, or safe failure behavior.
- [VP-0188](../done/VP-0188_config-scope-and-source-reload-consistency.md)
  established accepted configuration snapshots and explicit saved-config apply.
  This story closes the separate calibration-resource polling path.
- Shader pipeline preparation, screenshot readback, general logging, and wider
  rendering-thread architecture findings remain outside this implementation.

## Readiness review

Reviewed against remote beta `v1.4.02-beta` at
`fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02` on 2026-10-22. GitHub remote HEAD
also resolves to this beta. The user session's VP defaults and explicit scope
supply the implementation baseline; no historical local source HEAD is used.

- Current calibration settings and target slots are already represented in
  `RendererSettings`. `LoadDisplayLut` validates an off-side candidate, compares
  calibration contract identity, swaps on success, and can retain a compatible
  last-known-good LUT on failure.
- `MaybeReloadDisplayLut` performs the once-per-second file-version probe and
  calls `LoadDisplayLut`; its playback call is the narrow removal target.
- The explicit configuration acceptance path must carry a reload intent through
  unchanged-settings optimization, rather than relying on a changed filename or
  a filesystem timestamp. Existing owner-thread resource lifetime is retained.
- No new API, color-pipeline ordering change, background GPU-context owner, or
  dependency upgrade is required. Focused same-path and lifecycle regressions
  can prove the behavior. Actual playback timing is not claimed by static review.
- Implementation worktree is clean at the recorded beta baseline:
  `E:\codex\videoprocessor\lut-explicit-apply-20261022`, branch
  `codex/lut-explicit-apply-20261022`.

## Acceptance and validation

1. A selected valid LUT remains active after an in-place disk edit during
   simulated/production-connected playback; no periodic probe/reload path runs.
2. Apply/OK with otherwise identical accepted settings reloads new valid LUT
   bytes at the same path. Cover unchanged file identity where practical.
3. A malformed, missing, or unreadable same-path candidate on explicit apply
   retains the valid active LUT under the same contract and reports failure.
   Corrected bytes can be adopted on the next explicit apply.
4. Actual enable/disable, slot/target/path selection, startup, and rejection
   semantics remain safe; incompatible calibration contracts do not reuse bytes.
5. Run appropriate focused LUT/configuration regression tests and a successful
   x64 Release build. Record exact source commit, commands, and results before
   moving to Review. Independent code review must cover reload intent delivery
   and the absence of filesystem work from steady playback.
6. No deployment or live player/configuration modification is part of this task.

## Tracker audit

Origin/main `4b63b06a7d2249243024ae63b6f360ff996ba5b3` contained 221 canonical
records and 221 indexed rows: 200 roots and 21 children, highest root VP-0202.
Registry Last VP-0202 / Next VP-0203 / count 221 matched. No duplicate IDs,
missing canonical/index items, orphan children, or non-contiguous child sequences
were found. No existing open task covers this Apply-only LUT policy.

Historical formatting issues are reported without repair: VP-0070 lacks the
exact `## Status` heading; VP-0088 and VP-0168 use `In progress` in status text,
and VP-0168's index row uses the same noncanonical capitalization. Many older
status paragraphs combine the state with narrative. This task changes only
VP-0203 and its registry/index entries; it does not normalize historical records.
