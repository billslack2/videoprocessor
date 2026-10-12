# VP-0198: Add a contains operator to shared rule expressions

## Status

Backlog

Created 2026-10-11 at the user's request. Story creation only; implementation
has not started. Next action: perform implementation readiness review against
the then-current remote beta, trace shared parser consumers and Config
validation/help, and establish failing parser and integration regressions.
No implementation blocker is currently known.

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
${renderer} contains "VP Renderer" && ${source_rate} <= 30
```

The user also asked whether `~=` would be the right spelling. Use `contains`
as the proposed canonical spelling: it clearly communicates literal substring
matching, whereas `~=` can suggest approximate equality or pattern matching.
A symbolic alias is not required by this story; do not introduce regex,
wildcards, fuzzy matching, or approximate numeric comparison.

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

- Add an infix `contains` comparison between a known text variable and exactly
  one non-empty quoted string. Recognize the lowercase keyword only in operator
  position; preserve existing values or identifiers containing the word
  `contains`. Reject `CONTAINS` with a clear operator diagnostic unless a later
  explicit syntax decision adds case-insensitive keyword spelling.
- Accept both existing quote styles, for example `${renderer} contains
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
- Treat `contains` as one comparison atom, like `==`. Preserve grouping and
  unary negation of comparison atoms, then `&&`, then `||`. For example,
  `!(${renderer} contains "VP Renderer")` negates the substring comparison;
  the motivating source-rate expression means `(renderer contains substring)
  AND `(source_rate <= 30)`. Do not reinterpret existing expression precedence.
- Give a successful non-key contains comparison the same specificity weight as
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
   substring, every invalid operand category, unterminated quotes, keyword
   boundaries, and literal `contains` values in existing equality rules.
   Cover profile-name normalization and preservation of key-chord restrictions.
3. Composition tests prove `!`, parentheses, `&&`, and `||` precedence and
   evaluation behavior using mixed true/false comparisons. The rate boundary,
   numeric tolerance/ranges, equality/inequality, Boolean shorthand, legacy
   alternatives, and strict-mode restrictions keep their existing behavior.
4. Runtime selection and Config validation agree on valid and invalid examples.
   Exercise actual Config load/edit/save validation and reloading a temporary
   configuration, not only isolated helper tests. Valid contains expressions
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
