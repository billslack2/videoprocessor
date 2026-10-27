# VP-0218: Improve aspect transitions with current relative boundary evidence

## Status

In Progress

Implementation authorized: implement carefully with Luna subagent conflict/regression reviews, then deploy when checks pass. GitHub default/latest beta rediscovered as `v1.4.02-beta`, fetched at `7394ba420999a8fa708718ee4ce4d87e7459369f`. Active branch `codex/vp-0218-adaptive-ar-20261027`, isolated worktree `E:\codex\videoprocessor\vp-0218-adaptive-ar-20261027`. Production source work is underway; no candidate has yet passed or been deployed. Preserve experimental profile qualification gates and record actual achieved scope.

2026-10-26 review record: Three-reviewer source/design investigation completed against current remote beta `v1.4.02-beta` at `7394ba420999a8fa708718ee4ce4d87e7459369f`. Implementation had not started at that review. No production logic, configuration or deployed binary changed. The user reports slow scope-to-IMAX/16:9 transitions and frequent bright YouTube TV ads with clear borders that never acquire. The user then captured a paused ad: all 97 native payloads reproduce provisional acquisition, with a raised/nonuniform upper exterior and trusted near-black lower bar. This Limited-range failure requires a separate variable-profile investigation; the initial uniform, matching-matte proposal does not fix it. See [native capture findings](../assets/VP-0218/native-capture-20261026.md).

The design and validation contract are in [adaptive-evidence-design.md](../assets/VP-0218/adaptive-evidence-design.md); [evidence.md](../assets/VP-0218/evidence.md) records executed characterization and capture status. The source review uses `E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026`. Review checkout of the tracker is a clean `main` clone under `E:\codex\story-tracker\ar-dynamic-evidence-review-20261026`; the unrelated existing edit in the dedicated tracker is preserved.

## User story

As a viewer of mixed-aspect movies and YouTube TV, I want VP to use clear current differences between matte and picture to acquire and follow the correct format promptly, including raised bars and dark scenes, without hiding picture/captions, introducing framing chatter, or adding playback latency.

## Confirmed source findings

- Strong adaptive evidence already exists; this is not a proposal to replace a static detector wholesale.
- Native-distributed dark expansion is accepted live but lacks the ordinary equivalent buffered proof route.
- Exclusive broad/distributed route selection can restart proof even if one underlying predicate could stay continuously valid. Existing mixed-route rejection must be preserved by independent complete runs, not pooled votes.
- Rough nomination, Full-range matte qualification and final retention have different absolute black-level gates. A new raised-matte route must carry exact local reference authority through all relevant consumers.
- The actual ad is blocked by missing upper-bar nomination and excluded-band safety, with saturated confirmations and no near-black episode. Its top gradient also conflicts with absolute visible-content checks; both discovery and retention require typed per-edge proof, independent edge localization and explicit reconciliation of raw visibility witnesses. A strong boundary alone is insufficient.
- Effective AR darkness and raw-dark episode ownership are deliberately separate. A target-specific handoff needs its own evidence; changing a global threshold is insufficient.
- Full-raster endpoints need positive current extent support. Absence of recognized bars is not a substitute.

## Scope

1. Characterize current behavior using native paused/movie/ad evidence and parameterized unchanged-beta fixtures.
2. Reuse the existing native-distributed predicate in queued proof, preserving identity, confirmation, budget and final-admission requirements.
3. Evaluate broad/distributed predicates independently with separate contiguous runs, retaining one vote per accepted source identity per route and common bounded episode/owner provenance. Extend queued producer, consumer and progressive-onset checks together; keep subtitle-TRANSLATE retirement at its existing broad-only scope until separately qualified.
4. Design and separately qualify uniform-matte and spatially varying exterior routes for opposing neutral bands around bright picture. The recorded ad needs the latter, with independent per-edge references and boundary localization, outward uncertainty retention, constrained fitting with held-out validation, raw visibility-witness reconciliation, and residual/connected-picture/caption vetoes. Both routes require whole-band safety, exact context, typed raw-to-qualified publication, history/cache validation, and current-frame retention.
5. Separately evaluate dark-owner handoff and scope-to-full-raster support. Preserve title/fade, subtitle, startup, gradual-motion and windowbox safeguards.
6. Measure final submitted geometry and performance, not only detector classifications.

## Acceptance criteria

- Each behavior change has an unchanged-baseline failing positive and preserved adversarial negatives, including actual native ad/movie input where available.
- Raised-bar floor/range sweeps cover the discontinuities documented in the design; no scalar threshold increase substitutes for local boundary proof.
- Native-distributed queued transitions match the established broad route's timing when the same three source frames are available. No queue growth or waiting is introduced.
- Independent proof runs improve continuity only when one full route is valid throughout. Alternating incomplete routes, repeats, stale/mismatched identities and canceled work never accumulate authority.
- The captured ad must not be assigned top=302 solely from lower-bar symmetry or the diagnostic 299-to-304 step: row299 already contains red transition chroma. Independently annotated boundary/fringe and held-out ceiling/floor, grain/vignette and authored-graphic negatives determine whether the new route can be qualified.
- Current visible picture, captions and connected objects cannot be discarded by a robust statistical summary or an obsolete certificate.
- Final crop/fill/NLS/subtitle ownership makes one coherent transition; no new oscillation or unintended gradual-transition behavior appears in the agreed corpus.
- Raw darkness, AR-specific exception and episode release remain separately observable; evidence limitations cannot masquerade as a timer or gain authority from elapsed time.
- The applicable source/range/frame-rate/performance matrix, x64 Release build and regression checks pass, with physical playback acceptance explicitly recorded before closure. No universal zero-regression claim is inferred from a design review.

## Readiness and next action

Proceed first with reproducible evidence and the two contained proof-parity changes. Uniform-matte and variable-profile measurement begin as separate shadow routes until their exact thresholds, candidate-search bounds, whole-band inspection cost and final retention semantics are qualified. Dark-owner/full-raster extensions require additional labeled native positives and counterexamples before activation.

At implementation start, rediscover/fetch the latest remote beta, create a clean source worktree under `E:\codex\videoprocessor`, record its identity, and move this story and index to In Progress atomically. Use the user's standing current-beta baseline instruction. Do not deploy as part of this review/design task.

## Related work

VP-0035 and VP-0189: mixed-aspect responsiveness and stable recovery. VP-0136: unresolved false inward overlay transitions. VP-0165: safe lookahead/performance. VP-0201: startup framing. VP-0213 and VP-0214: final presentation ownership. Their statuses are not changed by this design.
