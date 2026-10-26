# VP-0214: Audit shared subtitle fit ownership and temporal recovery

## Status

In Progress

2026-10-25: Follow-up concern identified during the independent team assessment
for VP-0213. Investigation/implementation of shared subtitle behavior is not
part of VP-0213; no Classic/reconstruction policy change is made by that repair.

## Problem

Classic and reconstructed subtitle modes share a boundary between exact fresh
caption/UI fit proof, held presentation extents, uncertainty recovery, and
successfully submitted source envelopes. These have different lifetimes.
An old selected output is a record of displayed geometry, but must not become
fresh source evidence or bypass an owner's bounded release semantics.

VP-0213 confirms an Off incident: source n can recover the envelope last
submitted from n-2 because source n-1 is selected after preparing n. Held
geometry may fail exact fresh-proof equality, allowing alternating envelopes.
This establishes an architectural risk in shared code, not a confirmed
reproduction or approved blanket fix for Classic/reconstruction.

## Scope and next action

Audit each mode's legitimate fit, translation, drift, composition and recovery
ownership against source evidence and actual selected output. Reproduce any
shared-mode failure independently before changing behavior. Preserve current
glyph/masking proof, no-clipping safety, release timing, and real AR transitions.
Start from current GitHub beta under the normal source-worktree workflow.

## Acceptance criteria

1. Document which evidence can originate, retain, replace and release each
   Classic/reconstruction presentation owner, including skipped/unknown scans.
2. Replay mismatched fresh/held bounds and delayed/repeated/failed submissions
   through production recovery and final output. Prove or dismiss the risk
   separately for each mode; do not claim Off evidence proves a shared bug.
3. If a defect is reproduced, implement a bounded mode-appropriate correction
   with source identity and lifetime tests, preserving glyph proof, translation,
   drift and composition. Passing tests must verify final source/target geometry.
4. Validate Off remains governed by its independent VP-0213 fit-and-hold owner.
   Record Release build/regressions and live-playback limits before review.

## Dependencies and evidence

VP-0213 owns the Off-only fix. Related code at beta 8c453746:
UpdateVerticalBarPresentation, QualifyConfirmedFitOwnership,
HasConfirmedPresentationCandidate, ResolveUncertainPicturePresentation,
TemporalPresentationSnapshot and CommitSubmittedNativeScopeAnchor.
See VP-0213 incident/investigation assets for the independently observed Off
case and source/output ordering. This follow-up must not delay the isolated fix.

## Independent Reconstruction incident - 2026-10-25

VP-0207 now records a bright fixed-ratio live failure at 22:52:59-22:53:04, independently of the earlier Off reproduction. Trusted native picture remains 0,278-3840,1882 while actual submitted geometry expands to 0,270-3840,1960/1978 and returns to 36,278-3804,1882. Reconstruction composition loss precedes each expansion; prepared subtitles restore native framing. Source generation 2 and viewport 4 remain current, near_black=0, scene=0, temporal=0 and mixing=0. Later deliberate subtitle cycling does not explain this interval.

See [incident receipt](../assets/VP-0207/fixed-ratio-subtitle-handoff-20261025/incident.json) and [actual output excerpts](../assets/VP-0207/fixed-ratio-subtitle-handoff-20261025/incident-excerpt.txt). This confirms shared-mode presentation instability, but not that the Off feedback defect is its cause. No matching native pixel capture proves whether no-bright-ink was an incorrect scan. Investigation found no stale-worker/reset evidence in this interval. Reproduce cue handoff and recovery with final geometry assertions before implementation; preserve clipping safety and exact glyph/cleanup proof. The story remains Backlog during investigation; no production implementation started here.

## Coordinated ownership review - 2026-10-25

Five Luna extra-high reviewers independently examined and discussed the Reconstruction incident. The strongest supported mechanism is the mismatch between persistent raw envelope lifetime and per-frame prepared-caption projection: prepared proof suppresses a local copy, while completed negative scans can expose the same still-live history. This is more directly supported than a stale worker result, selected-output lag, expired hold or renderer reset in the onset interval. Underlying incorrect subtitle detection is not proven without native pixels.

The raw envelope lacks caption/UI provenance, so a blanket clear or coarse no-caption shortcut is not qualified. Next implementation needs a mode-appropriate owner/evidence contract and production handoff replay with final source/target assertions, including dim/sparse UI and unknown-scan controls. See [five-reviewer findings and test plan](../assets/VP-0207/five-luna-review-20261025/review.txt). No implementation began; status remains Backlog.

## Held-envelope production-function reproduction - 2026-10-25

The follow-up [logic investigation and native probe](../assets/VP-0207/held-envelope-logic-20261025/review.txt) reproduces qualified/unqualified/qualified native-expanded-native crop transitions using current beta policy functions. It identifies both retained envelope and dense FIT contributors, expansion surviving translation-confirmation ownership, and a separate skipped-analysis hold-expiry bypass. All probe assertions and expiry/generation controls pass; synthetic proof/routing fixtures are not an end-to-end image replay. No production implementation or deployment; existing story status remains unchanged.

Follow-up [safe protection review](../assets/VP-0207/held-envelope-logic-20261025/protection-review.txt) confirms a label-dependent recovery bypass and that strict clearance scheduling excludes no-bright-ink. Passing current-policy probe controls cover exact/stale UI proof and already-submitted envelopes. Recommends a bounded exact-frame full-row clearance certificate covering both retained contributors, plus geometry-based owner validation. Design only; no production patch or deployment.

2026-10-26: User authorized the narrow exact-frame clearance guard and diagnostic logging. Implementation started on codex/subtitle-rewind-ar-20261025 from current beta81ac960f. Validation in progress; no deployment.

## Narrow clear-row protection implemented - 2026-10-26

Implemented and pushed commit714e35df on codex/subtitle-rewind-ar-20261025. Advanced subtitle modes can reject combined historical vertical expansion only with exact current full-row clearance, current native authority and a matching submitted anchor. Added bounded asynchronous negative-frame scans, cache-safe request identity and diagnostic rejection/success logs. Classic/Off negative-scan behavior is unchanged. Clean x64 Release build passed;1174 focused subtitle/crop/recovery/GPU regressions and4 targeted checks passed (counts overlap). See [implementation, limitations and diagnostics](../assets/VP-0214/clear-row-guard-20261026/implementation.txt) and [validation receipt](../assets/VP-0214/clear-row-guard-20261026/validation.json). No beta merge, deployment or native incident video replay. Broader ownership audit remains In Progress.
