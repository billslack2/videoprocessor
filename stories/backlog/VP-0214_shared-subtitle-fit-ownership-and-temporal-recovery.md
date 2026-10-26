# VP-0214: Audit shared subtitle fit ownership and temporal recovery

## Status

Backlog

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
