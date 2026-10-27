# VP-0218: Improve aspect transitions with current relative boundary evidence

## Status

Backlog

2026-10-26: Three-reviewer source/design investigation completed against current remote beta `v1.4.02-beta` at `7394ba420999a8fa708718ee4ce4d87e7459369f`. Implementation has not started. No production logic, configuration or deployed binary changed. The user reports slow scope-to-IMAX/16:9 transitions and frequent bright YouTube TV ads with clear borders that never acquire. Raised black is a hypothesis to verify with native input.

The design and validation contract are in [adaptive-evidence-design.md](../assets/VP-0218/adaptive-evidence-design.md); [evidence.md](../assets/VP-0218/evidence.md) records executed characterization and capture status. The source review uses `E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026`. Review checkout of the tracker is a clean `main` clone under `E:\codex\story-tracker\ar-dynamic-evidence-review-20261026`; the unrelated existing edit in the dedicated tracker is preserved.

## User story

As a viewer of mixed-aspect movies and YouTube TV, I want VP to use clear current differences between matte and picture to acquire and follow the correct format promptly, including raised bars and dark scenes, without hiding picture/captions, introducing framing chatter, or adding playback latency.

## Confirmed source findings

- Strong adaptive evidence already exists; this is not a proposal to replace a static detector wholesale.
- Native-distributed dark expansion is accepted live but lacks the ordinary equivalent buffered proof route.
- Exclusive broad/distributed route selection can restart proof even if one underlying predicate could stay continuously valid. Existing mixed-route rejection must be preserved by independent complete runs, not pooled votes.
- Rough nomination, Full-range matte qualification and final retention have different absolute black-level gates. A new raised-matte route must carry exact local reference authority through all relevant consumers.
- Effective AR darkness and raw-dark episode ownership are deliberately separate. A target-specific handoff needs its own evidence; changing a global threshold is insufficient.
- Full-raster endpoints need positive current extent support. Absence of recognized bars is not a substitute.

## Scope

1. Characterize current behavior using native paused/movie/ad evidence and parameterized unchanged-beta fixtures.
2. Reuse the existing native-distributed predicate in queued proof, preserving identity, confirmation, budget and final-admission requirements.
3. Evaluate broad/distributed predicates independently with separate contiguous runs, retaining one vote per accepted source identity per route and common bounded episode/owner provenance. Extend queued producer, consumer and progressive-onset checks together; keep subtitle-TRANSLATE retirement at its existing broad-only scope until separately qualified.
4. Design and qualify a bounded relative-matte nomination and certificate path for opposing neutral bars around bright picture. It must include whole-band safety, local contrast/continuation, chroma and connected-picture vetoes, exact context, typed raw-to-qualified publication, history/cache validation, and current-frame retention.
5. Separately evaluate dark-owner handoff and scope-to-full-raster support. Preserve title/fade, subtitle, startup, gradual-motion and windowbox safeguards.
6. Measure final submitted geometry and performance, not only detector classifications.

## Acceptance criteria

- Each behavior change has an unchanged-baseline failing positive and preserved adversarial negatives, including actual native ad/movie input where available.
- Raised-bar floor/range sweeps cover the discontinuities documented in the design; no scalar threshold increase substitutes for local boundary proof.
- Native-distributed queued transitions match the established broad route's timing when the same three source frames are available. No queue growth or waiting is introduced.
- Independent proof runs improve continuity only when one full route is valid throughout. Alternating incomplete routes, repeats, stale/mismatched identities and canceled work never accumulate authority.
- Current visible picture, captions and connected objects cannot be discarded by a robust statistical summary or an obsolete certificate.
- Final crop/fill/NLS/subtitle ownership makes one coherent transition; no new oscillation or unintended gradual-transition behavior appears in the agreed corpus.
- Raw darkness, AR-specific exception and episode release remain separately observable; evidence limitations cannot masquerade as a timer or gain authority from elapsed time.
- The applicable source/range/frame-rate/performance matrix, x64 Release build and regression checks pass, with physical playback acceptance explicitly recorded before closure. No universal zero-regression claim is inferred from a design review.

## Readiness and next action

Proceed first with reproducible evidence and the two contained proof-parity changes. Relative-matte measurement begins in shadow mode until its exact thresholds, candidate-search bounds, whole-band inspection cost and final retention semantics are qualified. Dark-owner/full-raster extensions require additional labeled native positives and counterexamples before activation.

At implementation start, rediscover/fetch the latest remote beta, create a clean source worktree under `E:\codex\videoprocessor`, record its identity, and move this story and index to In Progress atomically. Use the user's standing current-beta baseline instruction. Do not deploy as part of this review/design task.

## Related work

VP-0035 and VP-0189: mixed-aspect responsiveness and stable recovery. VP-0136: unresolved false inward overlay transitions. VP-0165: safe lookahead/performance. VP-0201: startup framing. VP-0213 and VP-0214: final presentation ownership. Their statuses are not changed by this design.
