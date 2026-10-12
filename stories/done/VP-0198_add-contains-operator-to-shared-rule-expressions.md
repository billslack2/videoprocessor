# VP-0198: Add a symbolic substring operator (~=) to shared rule expressions

## Status

Done

Created and completed 2026-10-11. Implemented, merged in PR #145, and deployed
from the successful x64 Release build of merge commit
`a1853906806a459e33439718690e0f1ce7741b59`. The user authorized merge and then
deployment, explicitly requesting no additional tests before deployment.
The scoped tests passed; unrelated full-suite failures are disclosed below.

## User story

As a VideoProcessor user, I want one renderer substring condition to match
both VP Renderer and VP Renderer (Beta), so I can combine it with a source-rate
condition without repeating full renderer equality comparisons.

Replace:

```text
(${renderer} == "VP Renderer" || ${renderer} == "VP Renderer (Beta)") && ${source_rate} <= 30
```

with:

```text
${renderer} ~= "VP Renderer" && ${source_rate} <= 30
```

The user clarified that the operator must be symbolic. Use `~=` as the
canonical spelling for literal substring matching. Do not add a `contains`
keyword alias, regex, wildcards, fuzzy matching, or approximate numeric comparison.

## Context and reviewed behavior

Repository of record: `billslack2/videoprocessor`. GitHub's default branch was
verified as `v1.4.01-beta` during story creation. This is context, not a pinned
base for later implementation; discover the current remote beta when work starts.

[PR #143](https://github.com/billslack2/videoprocessor/pull/143), merged at
`d06012680e1cb6f68b8a3e05eb52ef9c80d397e4`, introduced VP Renderer (Beta).
Read-only review of `src/VideoProcessor-Lib/DisplayRuleExpression.h` at that
commit confirms equality, numeric comparisons, `&&`, `||`, `!`, and grouping,
but no substring operator. The AST is shared by validation and runtime consumers.

Text comparisons normalize case with `ConfigFile::NormalizeName`; profile
selection variables also use `NormalizeProfileComparisonName`. `${key}` is
special: it requires equality with one non-empty quoted shortcut and retains
case-sensitive chord semantics. Missing/empty actual values do not match.

A profile marked **Active** can be manually selected regardless of its rule.
A rule governs automatic matching, not permission to select an Active profile
manually. Preserve this distinction in runtime behavior and Config help.

## Proposed expression contract

- Add an infix `~=` comparison between a known text variable and exactly
  one non-empty quoted string. Recognize the two-character operator as one
  token, with or without surrounding whitespace: `${renderer}~="VP Renderer"`
  and `${renderer} ~= "VP Renderer"` are equivalent. Reject malformed operator
  spellings such as `~`, `~==`, and `~ =` with a clear diagnostic. Preserve
  literal `~=` inside quoted values and existing text values containing `~`.
- Accept both existing quote styles, for example `${renderer} ~=
  'VP Renderer'`. Preserve existing quote parsing: spaces and punctuation inside
  quotes are data; do not silently introduce a new escaping convention. Test
  unterminated quotes and the existing handling of quote characters/backslashes.
- Match a contiguous literal substring anywhere in the normalized actual text.
  Use the same normalization on both operands as existing equality for that
  variable, including profile-selection normalization. Renderer matching is
  therefore case-insensitive. No new locale/Unicode folding or trimming policy.
  Literal `*`, `?`, and regex punctuation have no special matching meaning.
- Reject an empty substring, including a value made empty by existing
  normalization, at compilation/validation time. Do not turn it into a rule
  that matches every renderer. A missing/empty actual value remains a non-match;
  existing outer `!` behavior remains unchanged.
- Reject numeric and Boolean left operands, `${key}`, unknown variables,
  missing operands, unquoted right operands, variable-to-variable operands,
  multiple alternatives, and chained comparisons with useful diagnostics.
  A quoted `${...}` on the right is literal text, not interpolation. Preserve
  existing context-specific variable restrictions; adding an operator must not
  make renderer variables available in previously unsupported action contexts.
- Treat `~=` as one comparison atom, like `==`. Preserve grouping and
  unary negation of comparison atoms, then `&&`, then `||`. For example,
  `!(${renderer} ~= "VP Renderer")` negates the substring comparison;
  the motivating source-rate expression means `(renderer ~= substring)
  AND `(source_rate <= 30)`. Do not reinterpret existing expression precedence.
- Give a successful non-key substring comparison the same specificity weight as
  one existing non-key comparison. Preserve selection priority/tie behavior,
  dependency discovery, and strict versus legacy grammar rules. Support the
  operator in both shared parser modes without loosening old syntax checks.

## Scope

Implement the operator in the shared lexer/parser/AST/evaluator and make all
applicable runtime rule consumers use the same semantics. Config must validate,
load, edit, save, and round-trip it wherever that shared expression language is
supported. Update operator help, validation messages, and the canonical
configuration reference with the combined renderer/source-rate example,
case/quote/type/empty-value rules, precedence, and the Active/manual distinction.

No renderer renaming, profile-selection policy redesign, configuration migration,
or unrelated expression syntax expansion is required. Story creation does not
change source, deploy binaries, or edit the user's active configuration.

## Acceptance criteria and validation

1. The exact motivating expression compiles and matches `VP Renderer` and
   `VP Renderer (Beta)` at source rates 24 and 30, but neither at a rate above
   30. It does not match `madVR` or another unrelated renderer at 24. Include
   mixed-case renderer/substring cases and a substring in the middle of text.
2. Shared parser tests cover both quote styles, literal punctuation and spaces,
   repeated text, non-matches, missing/empty actual values, rejected empty
   substring, every invalid operand category, unterminated quotes, operator
   token boundaries, optional whitespace, malformed symbolic operators, and
   literal `~=` and `contains` values in existing equality rules.
   Cover profile-name normalization and preservation of key-chord restrictions.
3. Composition tests prove `!`, parentheses, `&&`, and `||` precedence and
   evaluation behavior using mixed true/false comparisons. The rate boundary,
   numeric tolerance/ranges, equality/inequality, Boolean shorthand, legacy
   alternatives, and strict-mode restrictions keep their existing behavior.
4. Runtime selection and Config validation agree on valid and invalid examples.
   Exercise actual Config load/edit/save validation and reloading a temporary
   configuration, not only isolated helper tests. Valid `~=` expressions
   survive round-trip unchanged; invalid expressions produce actionable errors
   identifying the offending rule/operator/operand.
5. Production-connected rule-selection tests prove the combined condition works
   for both renderer names, including reevaluation after a renderer swap, and
   preserves existing priority/specificity and variable dependency behavior.
   An Active profile whose rule is false remains manually selectable, while
   automatic selection observes the rule; existing inactive-profile behavior
   remains unchanged.
6. Config help and the canonical reference show the exact simplified rule and
   explain literal case-insensitive renderer matching, supported operands,
   quotes, precedence, empty-substring rejection, and manual selection. All
   previously supported rules continue to pass the existing regression suites.
7. Before moving to Review, record failing-before/passing-after targeted parser,
   runtime, and Config results and a successful x64 Release build. Any future
   deployment is a separate authorized action and must preserve active settings.

## Tracker creation audit

Before assignment, all six canonical state folders and the index were audited:
216 files and 216 indexed items; no duplicate or missing IDs, orphan children,
child-sequence gaps, or registry/count mismatch; highest assigned root VP-0197.
VP-0198 is the next unused root. This story and its index row add one item.

Historical status formatting does not uniformly satisfy the exact-state-line
convention: many records append narrative to their status line; VP-0070 has no
standard `## Status` match, and VP-0168 uses `In progress` capitalization in the
index and record. Decision for this bounded creation task: preserve historical
records, report these discrepancies, and defer any tracker-wide repair to a
separate deliberate change. No unrelated story is renumbered or moved.

## Implementation readiness — 2026-10-11

User authorized implementation, testing, push, and merge into the latest default.
Verified latest beta/default: v1.4.01-beta at 7e37c842.
Worktree: E:\codex\videoprocessor\vp-0198-substring; branch: codex/vp-0198-substring.
Shared parser/AST and Config validation use the same expression implementation;
existing native tests cover parser, Config document save/reload, and profile runtime.
No new dependencies or resource-lifetime changes are required. Add failing tests,
then extend comparison handling and help; run native regressions and x64 Release.

## Completion and validation evidence — 2026-10-11

- Implementation: `c21ce93a5e63cf5730dbe8607a0d60728181798e`;
  final feature including operator-summary documentation:
  `f1a369e8b107170ff068e69414aa4fb08ffb3a78`.
- [PR #145](https://github.com/billslack2/videoprocessor/pull/145) merged into
  the verified latest beta/default `v1.4.01-beta` at
  `a1853906806a459e33439718690e0f1ce7741b59`. The merge tree was verified
  identical to the reviewed feature tree. The merge helper enforced source/base
  pins and GitHub check status.
- Before implementation, three positive substring/Config regressions failed;
  invalid-operand rejection passed. After implementation all five new tests
  passed: renderer/rate matrix, grammar/normalization, invalid operands,
  runtime refresh/manual selection, and Config edit/save/reload validation.
- Focused offscreen Config UI regression `disabling shader rule preserves
  shortcut` passed before the user's instruction to stop adding tests.
- Full native suite completed with **2,375 passed, 7 failed, 0 skipped**.
  This is not a claim of a fully passing suite. Six failures are in unchanged
  subtitle detector tests/code: `JoinedArabicWordWiderThanEightGlyphHeightsIsNotClipped`,
  `WideWordSpaceKeepsBothSubtitleWordsInOneLine`,
  `LargeConnectedArabicWordSurvivesEveryUhdSamplingPhase`,
  `CardBoundaryKeepsCorroboratedJoinedScriptStrokes`,
  `TinyPictureSideDotsRemainOwnedWithTheirJoinedScriptRow`, and
  `ShortArabicCompanionMayAlignToEitherEdgeOfBarAnchor`.
  `LibplaceboPluginApiVersionCoversConfigurationSnapshots` expects 24 while
  the base defines 25. The version mismatch was verified directly in the base;
  subtitle files have no diff from the base. A baseline rerun was not performed.
  These unrelated failures were disclosed before merge and left unchanged.
- Build: full solution x64 Release succeeded. An earlier incremental test link
  reported LNK1103 in `AlphaSourceCropPolicyTests.obj`; rebuilding that generated
  object and the test linker cache resolved it. The merged commit then received
  another successful x64 Release build for deployment identity; no additional
  tests ran after the user's no-more-tests instruction.
- Local evidence in `E:\codex\videoprocessor\vp-0198-substring`:
  `TestResults/substring-before.trx`, `substring-after.trx`, `full-suite.trx`,
  `build-final.log`, and `build-merged.log`.

## Deployment evidence

- Destination: `C:\Videoprocessor\vp`.
- Backup: `C:\Videoprocessor\vp\deployment-backups\vp-0198-20261011-210233`.
  Existing host, stable/Beta renderer DLLs, Config, Config discovery DLL, their
  runtime receipts, and `docs/CONFIGURATION.html` were backed up and replaced.
- All five binaries had valid x64 Release receipts; all 11 deployed files
  matched their build/source SHA-256 hashes. Hash evidence is stored in
  [deployed-hashes.json](../assets/VP-0198/deployed-hashes.json).
- Six active root settings files retained their hashes. No configuration edits
  were made. `VideoProcessor.cfg` SHA-256 remained
  `082998E42D20050C9E1431F2F346307D19FCBAB0C569FB78F546C7EED13E5761`.
- The hidden background Config process acknowledged graceful updater shutdown
  and was restarted with its original config/discovery/background arguments.
  VP itself was already closed and remains closed. No playback smoke test was
  run, as requested.
- The first deployment verification incorrectly included an intentionally
  replaced `.runtime.json` build receipt among settings; it restored the backed-up
  files. Excluding build receipts from the settings check, the final deployment
  completed and verified successfully. Original backups were preserved.
