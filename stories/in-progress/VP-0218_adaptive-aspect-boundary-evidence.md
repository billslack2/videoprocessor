# VP-0218: Improve aspect transitions with current relative boundary evidence

## Status

In Progress

Earlier contained AR improvements redeployed on 2026-10-27 from clean x64 Release commit `f4219a7aa679c57f1f6d637cb08406b9c2b4b97b`, rebased at the user's explicit request onto latest local subtitle build `f22e22c4f0dcd9be87c26189fc18a83a798c9a26`. Both prior AR patches are preserved exactly, alongside the latest subtitle guard/internal padding changes. The complete combined native suite passed 3439/3439, including every prior effective test; the focused Config guard test passed. Two Luna reviewers found no remaining integration/deployment blocker. Host, both renderer variants, Config/discovery and backups were hash-verified; active configuration/state remained unchanged. Both applications were already closed and were left closed, so runtime/physical playback acceptance is pending. The clean integration branch is pushed; no PR was merged. See [combined-build deployment evidence](../assets/VP-0218/local-integration-20261027/README.md). The earlier 01:13 deployment and original repaired-run evidence remain recorded in the evidence log.

The story remains In Progress because the captured variable-gradient YouTube TV ad is unchanged, and dark-owner/full-raster extensions remain unqualified. The shipped scope is bounded uniform raised-matte qualification, independent live proof continuity and native queued-proof parity. See [executed implementation and deployment evidence](../assets/VP-0218/evidence.md#2026-10-27-implementation-and-deployment).

2026-10-27 follow-up research: Three Luna reviewers tested independent relative-edge nomination, local exterior profiles, corrected paired synthetic controls and two older native captures against freshly fetched beta. Both ad borders are found on 97/97 saved frames, but the full exploratory safety route still rejects the upper texture. The existing deployed Release passes 149 distinct focused/captured-frame tests; this is baseline coverage, not an integrated new-route qualification. No production/configuration/deployment change was made. See [research findings and proposed contract](../assets/VP-0218/research-20261027/README.md).

2026-10-27 continued support research: The user's visually clear border assessment is supported by the independent measurements. A fixed 72-coefficient, spatially cross-validated reference now explains the upper gradient under existing edge-summary tolerances; the earlier component veto was an exploratory rule, not proof the sample is unsupported. Raw-witness reconciliation removes model-induced flags below the original cutoff, leaving five sampled localized details. However, ten independently generated 4K controls reveal potential new false crops for tiny/faint text, stars and logos if either relaxed morphology or sparse relative visibility becomes crop authority. Three additional decoder-verified native captures expose actual caption-preservation and sampling-phase requirements. The next implementation contract separates nominal geometry, per-edge background evidence and protected foreground/caption extent. No new crop route was enabled. See [support research and regression cases](../assets/VP-0218/support-20261027/README.md).

2026-10-26 review record: Three-reviewer source/design investigation completed against current remote beta `v1.4.02-beta` at `7394ba420999a8fa708718ee4ce4d87e7459369f`. Implementation had not started at that review. No production logic, configuration or deployed binary changed. The user reports slow scope-to-IMAX/16:9 transitions and frequent bright YouTube TV ads with clear borders that never acquire. The user then captured a paused ad: all 97 native payloads reproduce provisional acquisition, with a raised/nonuniform upper exterior and trusted near-black lower bar. This Limited-range failure requires a separate variable-profile investigation; the initial uniform, matching-matte proposal does not fix it. See [native capture findings](../assets/VP-0218/native-capture-20261026.md).

The design and validation contract are in [adaptive-evidence-design.md](../assets/VP-0218/adaptive-evidence-design.md); [evidence.md](../assets/VP-0218/evidence.md) records executed characterization and capture status. The source review uses `E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026`. Review checkout of the tracker is a clean `main` clone under `E:\codex\story-tracker\ar-dynamic-evidence-review-20261026`; the unrelated existing edit in the dedicated tracker is preserved.

## User story

As a viewer of mixed-aspect movies and YouTube TV, I want VP to use clear current differences between matte and picture to acquire and follow the correct format promptly, including raised bars and dark scenes, without hiding picture/captions, introducing framing chatter, or adding playback latency.

## Detection objective and display context

The user's objective is reliable, accurate detection of black-bar boundaries and the actual active-picture aspect ratio from source pixels, without regressions. This is independent of the physical screen, CIH, fitting, zoom, fill limits and NLS. Derive the aspect from the measured active-picture bounds; do not force a screen-sized or standard-aspect rectangle. Assess correct boundaries/aspect, missed bars, false crops, picture/caption preservation, transition responsiveness and stability. Check downstream rendering as integration coverage, not as the definition or source of detector authority.

The 2.35:1 CIH scope screen is confirmed display context. Scope viewport/zoom, automatic cropping, 2.32/2.42 fill limits and NLS Off explain presentation effects, but must not determine which pixels the detector calls bars. A 16:9 transport can carry a different active aspect. Full-raster retention alone therefore proves neither successful detection nor correct presentation. No user choice of a fitting mode is needed to continue the detection investigation. See the [capture interpretation](../assets/VP-0218/native-capture-20261026.md#cih-interpretation-corrected-on-2026-10-27).

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

The earlier contained implementation and its deployment are recorded above. Verify actual moving scope-to-taller transitions and final composition during playback before closing the shipped portion. The completed tests preserve stale/repeated identity rejection, independently complete proof routes, bounded owner age, anchored detector-step tolerance, progressive motion and subtitle ownership guards.

Continue variable-profile work as an additive route using the [latest support design](../assets/VP-0218/support-20261027/README.md). First emit separate measured-boundary, validated-reference and protected-foreground diagnostics. The fixed spatial reference now explains the ad under existing summary tolerances; it is not yet crop authority. The relaxed component-area rule and sparse relative visibility are insufficient: they can newly admit tiny/faint glyphs, stars and logos that beta preserves. A foreground-preservation stage must pass these cases without undoing the demonstrated background fit.

Keep the original nominal-boundary visibility/continuation scan alongside guarded-band validation; the real Thai caption demonstrates that changing sampling phase can lose a witness. Preserve existing subtitle ownership and distinct picture-aspect/caption-envelope outputs. Pair candidate replay against the ad, all five older native captures and the independent controls, retaining the working strict/dark route when the new cue abstains. A caption mask must not supply background or aspect authority.

Moving-transition, source identity/owner, budget and physical playback acceptance remain required. Dark-owner and full-raster extensions are separately unqualified. Neither the earlier 149 passing existing tests nor same-frame spatial model validation establishes a safe new production route or universal absence of regressions.

## Related work

VP-0035 and VP-0189: mixed-aspect responsiveness and stable recovery. VP-0136: unresolved false inward overlay transitions. VP-0165: safe lookahead/performance. VP-0201: startup framing. VP-0213 and VP-0214: final presentation ownership. Their statuses are not changed by this design.
