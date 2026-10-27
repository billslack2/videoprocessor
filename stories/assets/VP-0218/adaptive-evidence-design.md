# Adaptive aspect-ratio evidence: review and proposed design

Review date: 2026-10-26 (America/New_York). Source: `billslack2/videoprocessor`, current GitHub beta/default branch `v1.4.02-beta`, fetched at `7394ba420999a8fa708718ee4ce4d87e7459369f`. Review worktree: `E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026`.

This is a design review, not an implemented fix or a claim of regression-free playback. Three independent reviewers covered transition timing, bar/dark-picture evidence, and regression safety. The user reports slow scope-to-IMAX/16:9 changes and frequent bright YouTube TV ads with obvious borders that never acquire. Elevated black levels are the user's hypothesis; actual input values must be measured.

## Recommended direction

Use strong current evidence more consistently, with separate proofs for each kind of scene. First extend the existing dark-picture proof into lookahead and remove priority-induced proof restarts. In parallel, qualify a separate relative-matte route for the bright-ad failure. Treat dark-episode ownership and dark full-raster endpoints as separate changes with additional tests. Preserve existing confirmation counts until evidence shows a remaining timing problem.

The objective is correct, stable framing: promptly follow real aspect changes, preserve actual picture and captions, stay steady through ambiguous scenes, and avoid added queue delay. A strong border is a useful observation; it is not by itself permission to discard everything outside it.

## What is already implemented

The detector already adapts its black reference every frame, qualifies some raised mattes, compares local boundaries, checks distributed dark-picture texture, learns recurring geometry, and uses already-buffered future frames. There is no temporal learned black-level calibration in the inspected paths; recurring geometry still requires current pixel proof.

Once all gates pass, ordinary live outward confirmation takes three source samples, followed by the second transition-model vote on the fourth changed frame. A change beginning at sequence 100 is published at 103: approximately 125 ms at 24 fps, not a multi-second timer. Existing broad-picture lookahead can present the new geometry on the first changed frame with two suitable future frames already queued. Long failures therefore need measurement, proof-reset, ownership, and final-submission diagnosis before changing timers.

Current evidence sources (all links are pinned to the reviewed commit):

- [Black reference, raised matte, edge scans and local boundaries](https://github.com/billslack2/videoprocessor/blob/7394ba420999a8fa708718ee4ce4d87e7459369f/src/VideoProcessor-Lib/ActivePictureEvidence.cpp#L129).
- [Live outward proof and route selection](https://github.com/billslack2/videoprocessor/blob/7394ba420999a8fa708718ee4ce4d87e7459369f/src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.cpp#L579).
- [Buffered expansion evidence](https://github.com/billslack2/videoprocessor/blob/7394ba420999a8fa708718ee4ce4d87e7459369f/src/VideoProcessor-Lib/vprenderer/BufferedPictureExpansion.cpp#L109).
- [First-changed-frame and live timing regression](https://github.com/billslack2/videoprocessor/blob/7394ba420999a8fa708718ee4ce4d87e7459369f/src/VideoProcessor-Test/LookaheadOutwardProofTests.cpp#L996).

## Findings and engineering priorities

| Priority | Finding | Proposed change | Safety boundary |
| --- | --- | --- | --- |
| A | `BufferedPictureExpansion.cpp:109-124,697` requires broad occupancy; the already accepted native distributed route is not an equivalent queued route. | Add a distinct queued native-distributed certificate, applying the unchanged live predicate to every required source frame. | Same three real frame identities, current exact target, movement/owner vetoes, consumer revalidation; no extra queue wait. |
| A | `AlphaSourceCropPolicy.cpp:629,649` only evaluates native-distributed support when broad occupancy fails, then resets on selected-route changes. | Evaluate predicates independently and retain an independent contiguous run for each route. | Never pool incomplete broad-only and distributed-only runs. A route must pass on every contributor to its own certificate. |
| A, qualification required | Absolute bar thresholds can prevent otherwise clear raised-matte candidates from reaching structural checks. | Add bounded relative-matte nomination and a separate complete boundary/retention certificate. | Nomination has zero authority; no global black-threshold increase; visible picture and captions remain protected. |
| B | Effective AR darkness can be rebutted while raw-dark episode ownership still blocks acquisition. | Shadow a target-specific obsolete-owner handoff using current native boundaries and independently certified expansion. | Keep raw darkness for fades/titles. Releasing an obsolete owner does not directly publish an arbitrary crop. |
| B | Existing bar-dependent dark routes cannot certify a full-raster endpoint after bars disappear. | Separate proof that the old crop covers current picture from proof of the exact new active rectangle; qualify positive outer-extent support for 16:9. | Missing bar evidence is not full-raster proof. Do not change intentional gradual-expansion holding policy incidentally. |

### Complete queued-route integration

The native-distributed change must update typed certificate production, current-frame consumer validation and progressive-onset suppression together. `BufferedPictureExpansion.cpp:899,923` currently rechecks broad evidence in the latter paths; changing the producer alone will either be ineffective or leave inconsistent safety checks. A progressive dark-window regression must prove that the new queued certificate cannot publish an intermediate rectangle before the motion tracker recognizes expansion.

Preserve the existing broad-only scope of queued subtitle-TRANSLATE retirement (`BufferedPictureExpansion.cpp:943-950`) initially. A distributed certificate may accelerate a transition only where current owner policy already allows it; extending retirement to that route is separately qualified work.

Independent broad/distributed runs keep their own anchors, predicate results and votes, but share the same bounded presentation episode and original owner provenance. Selecting a completed route must not reset the common first-picture sequence, clear prior vertical-presentation ownership, restart the suppression budget, or fabricate an owner-free episode. Existing handoff checks use `firstPictureSourceSequence` and `verticalPresentationSeen` (`AlphaSourceCropPolicy.cpp:950-960,1039-1042`). Include alternating routes with existing TRANSLATE, FIT and drift owners; require unchanged owner eligibility and bounded handoff lifetime.
### Why bright raised bars can fail

All numbers below are in the detector's ten-bit analysis-luma domain, not screen brightness or raw eight-bit UI RGB.

`ActivePictureEvidence.cpp:1062-1084` estimates perimeter P10, forces floor 0 below observed code 32, otherwise caps floor at 80, and caps cutoff at 104. Therefore uniform bars at 25-31 can miss rough nomination with cutoff 24; bars above 104 also cannot pass the ordinary black-line nomination. Full-range matte qualification at line 168 additionally rejects measured Y above 80. A uniform 81-104 band can be nominated but rejected, after which nominal full-extent support above 48 can produce full-raster authority (`:1319-1332`). Limited and Unknown range follow different paths; they must be evaluated separately.

A 39-case unchanged-beta P210 characterization confirmed these discontinuities; 15 cases missed the authored bars. See [executed evidence](evidence.md). Do not claim the user's ads occupy any particular range until native evidence establishes it. The existing positive matte matrix primarily covers 0, 40 and 64 (`ActivePictureBlackReferenceTests.cpp:131-155`).

A nomination-only change is insufficient: retention separately caps P90 at 104 (`ActivePictureEvidence.cpp:589-601`), while strip resampling caps the floor/cutoff again (`:2618`). A local matte certificate must remain meaningful through current-frame retention and final admission, or detection may immediately lose authority and oscillate.

### Relative-matte certificate

Start with clear, stationary, opposing, neutral mattes around a full-width bright picture, on one cropped axis. Preserve current symmetry, orthogonal-axis and windowbox rules. Do not initially expand the scope to arbitrary tinted frames, slides, decorative borders or four-sided graphics.

1. **Nominate without calling the band black.** From bounded edge profiles, find a flat exterior region and a common abrupt transition. Record exact candidate, independent per-edge Y/U/V references, noise, uncertainty, support, scan completeness and sampling phase. Proposal generation does not modify the current trusted rectangle.
2. **Verify the whole proposed exterior.** Inspect multiple depths from raster edge to boundary and separated flanks/center segments. Opposing references must agree. Start with current conservative dispersion/agreement limits rather than relaxing level, noise and chroma together. Budget exhaustion is unavailable evidence, never safe matte.
3. **Verify local boundary contrast.** Compare paired inside/outside samples at the same columns, across every spatial quartile on both edges, then require picture continuation through multiple interior depths. A candidate seed is `delta >= max(absolute_minimum, k * exterior_noise)` with a quantization floor; `k` is uncalibrated, and is not a shipping constant. Preserve the current ordinary/thin-bar contrast minima initially (10/18 analysis codes). High noise makes qualification harder.
4. **Preserve contradiction checks independently.** A robust median must not hide a thin object, caption, logo, connected picture, colored detail or disagreement between candidate and final boundary. Inspect the final excluded region and its adjacent rows. Recognized captions remain a separate presentation/cleanup responsibility; relative matte authority never authorizes text erasure.
5. **Issue an exact, typed certificate.** Bind source identity, native/converted format and range, analysis domain, generation/epoch/continuity/policy, base/target, cropped axis, exact inspected bands and reference statistics. A nominal full-raster interpretation produced only by absolute magnitude must be distinguishable from positive structured picture in the proposed removed bands. Actual picture contradiction still wins.
6. **Carry it through retention.** Use its per-region reference only inside those certified excluded bands on the consumed frame. Do not overwrite the global black threshold, reuse a top reference for a different bottom band, or apply it to picture interior or a later context. A new source frame must be measured again. Preserve current final crop, optional-fill, subtitle and NLS arbitration.
7. **Use existing temporal safeguards.** Stationary strong evidence may use existing adjacent confirmation and available lookahead. Weak evidence cannot become authority merely by waiting. Moving edges retain the current motion/settlement contract. Do not introduce a multi-second blanket dwell.

A remembered bar/noise reference is not necessary for the first implementation. If later justified, it may only nominate; it must be frozen during ambiguity, scoped to source/domain, reset on context changes, and independently revalidated on current pixels. Never gradually learn picture into the black reference.

### Raw evidence, qualification and publication

Keep the original magnitude-based measurement intact for diagnostics and existing consumers. A nomination remains an unauthoritative alternative. Only a completed, exact-source relative-matte certificate may produce a qualified observation that competes with a legacy full-raster result caused solely by absolute magnitude. Positive picture structure/continuation in a proposed removed band vetoes that projection; disagreement stays explicit uncertainty.

The qualified observation must carry a distinct measurement route and its complete certificate through the transition model, timeline/lookahead, cache comparisons, current-frame retention and final consumer admission. Do not mark a proposal generically native to bypass existing non-native/deferred checks. Update the specific consumers to understand the new measured route and its per-axis proof. Remembered geometry cannot canonicalize an unproved target, discard its certificate, or lend old authority to new coordinates. Test raw-versus-qualified outcomes, history present/absent, every authority-bearing field mismatch, and next-frame certificate loss through actual submitted geometry.
### Dark-scene and scope-to-full-raster design

The current AR-only darkness exception requires native top/bottom bars, a bar-relative interior median increase of 32, 12/16 textured tiles across all four quadrants, and tile-median spread of 16 (`ActivePictureEvidence.cpp:2194-2305`). It is deliberately stricter than the human observation that one region has visible definition. Global P90 alone must not decide whether the newly exposed strip contains picture.

Keep three separate facts: (a) remaining exterior bands are safe, (b) newly exposed regions contain distributed current picture, and (c) a precise destination rectangle is supported. Local detail can contradict the old scope crop without proving an arbitrary replacement aspect. For a dark full-raster endpoint, certify appropriate outer extents and continuous picture support rather than inventing residual bars. Sparse stars, isolated text or one-edge overlays require their existing visibility-preserving presentation handling, not automatic permanent 16:9 classification.

`AlphaSourceCropPolicy.cpp:2939,2972-2979` intentionally uses raw darkness for near-black episodes, while `LibplaceboVideoRenderer.cpp:11511-11524` blocks acquisition during episode ownership. A new target-specific handoff must identify the obsolete owner, prove the changed target independently over fresh source frames and re-run final admission. Do not replace raw darkness everywhere with the AR exception. Preserve title cards, uniform fades, startup guards and source resets.

The gradual-expansion policy intentionally holds the established scope crop while verified edges move, then settles an intermediate endpoint for about 250 ms. That previously accepted behavior is distinct from missed hard cuts and must remain in this change's regression controls.

## Frame identity and evidence accounting

- One accepted source identity contributes at most one temporal vote to a given proof. Output repeats, cache hits, worker retries and live/lookahead reuse add none.
- Texture, chroma, brightness and contrast computed from overlapping pixels are features of one observation, not independent votes.
- Both opposing edges must be supported on the same frame. Do not combine yesterday's top with today's bottom.
- Each proof route has independent contiguous evidence; a complete certificate can be chosen, but incomplete routes cannot be summed.
- Keep unavailable, pending, rejected and completed-positive results distinct. Cancellation and stale evidence cannot become black/safe.
- New authority fields participate in cache comparisons and consumer validation. Context changes invalidate them; anchored scan-step tolerance cannot accumulate drift.
- Prepared future proof applies only when the selected frame and all relevant owner/context checks match. It cannot advance acquisition on a different frame or bypass `transitionDeferred`.

## Validation and acceptance

Use unchanged-beta characterization first, then red/green tests of each proposed behavior. Preserve negative fixtures and compare actual submitted source/target geometry, including subtitles, optional fill and NLS. A detector-only improvement is not sufficient.

| Corpus family | Required controls |
| --- | --- |
| Raised matte | Floors 0,24,25,31,32,40,64,80,81,90,104,105,128 across Full/Limited/Unknown, exact/noisy/tinted bars, thin/wide bars, both sampling phases; bright interior plus actual captured ad. |
| Dark picture | Low-contrast opposing boundaries, one bright/one dark strip, native dark IMAX and 16:9 endpoints; sparse stars, credits, uniform noise/fades and localized lights as negatives. |
| Spatial ambiguity | Thin outlines, bright artwork on black, gray ceiling/floor, smooth ramps, displaced boundaries, compression blocks, narrow connected objects and four-sided windows. |
| Temporal behavior | Hard cuts, alternating broad/distributed eligibility, gradual expansion, pauses/reversals, detector-step jitter, one-frame flashes and seven-second overlay false-inward trace from VP-0136. |
| Ownership | Existing/new captions, logos/controls, subtitle FIT/translation, Off/reconstruction, near-black owners, startup, history-qualified and history-free transitions. |
| Identity and scheduling | Repeated pixels with new capture identities versus same-identity display repeats, gaps, duplicates, out-of-order/late workers, source/range/domain/policy changes, cancellation, queue starvation and zero optional budget. |

Cross relevant cases with 1080p/2160p, supported native RGB/YUV/decoded-DV domains, 23.976/24/25/29.97/30/50/59.94/60 source rates and mismatched display cadence. Use parameterized focused cases where a full Cartesian product adds no new behavior.

Record latency from both actual format change and first sufficient evidence: measurement ready, certificate ready, accepted logical geometry and first submitted new rectangle. Also record first outside-picture witness, proof-reset reason, route, rejected owner, configured/available lookahead, sampled pixels, analysis p50/p95/p99/max, missed deadlines, repeats/drops and queue depth. Rate-limited log record counts are not frame frequencies or measured time spent in a state.

Acceptance requires zero newly hidden annotated picture/caption pixels and zero added false final-layout transitions in the agreed corpus. Preserve first-changed-frame application when the existing two-future-frame proof is available; otherwise never wait to fill the queue. For native-distributed parity, target the same live-fourth-frame / eligible-buffered-first-frame behavior as broad occupancy, subject to unchanged owner vetoes. Establish numeric cost limits from the baseline and existing optional-work allowance before activation; do not choose arbitrary performance targets after seeing results.

Initial implementation order:

1. Capture and characterize reported failures; add baseline route/reset/owner diagnostics only where existing records are insufficient.
2. Extend native-distributed queued proof and independent route runs, with unchanged spatial predicates and counts.
3. Implement relative-matte measurement in shadow mode; qualify native ad evidence plus counterexamples, then integrate the same certificate through acquisition and retention.
4. Independently qualify dark-episode handoff and dark full-raster endpoint support.
5. Rebase against the then-current beta, complete matching x64 Release builds and applicable native/GPU/regression checks, and perform requested live validation. Deployment remains a separate user-requested step.

## Evidence gathered in this review

The deployed log identifies host and renderer source `326012fd1c2f0f038815b98d526da83897972d20`. Although the source commits differ, Git blob comparisons confirm the inspected evidence headers/implementation, transition model, decision timeline, crop policy/header, buffered expansion and renderer implementation are byte-identical to reviewed beta `7394ba42`. This is source parity, not a new deployed-binary hash audit.

A local snapshot covers 22:07:52-22:59:11 on 2026-10-26. It includes near-black acquisition, untrusted candidates, outward proof restarts, native-distributed pending/confirmed cases and Full-range raised-reference rejection. For example, at 22:45:03 and 22:45:08 the measured target is `0,68-3840,2092` against `0,276-3840,1884`, with native-distributed proof pending; subsequent records show target acquisition. Rate limiting, changing scene/epoch and lack of labels prevent attributing a duration or cause to a particular movie/ad. The log is not proof of an indefinitely stuck transition.

The user subsequently paused a current example. The Windows computer-use helper failed twice before capture, so no screenshot was obtained by that tool. The associated log was preserved, and the user was directed to the built-in **Capture analysis diagnostics with input frames...** command. Native capture status and any additional characterization results are recorded in the companion evidence note. Original input pixels stay local, outside source control.

Local diagnostic directory: `E:\codex\diagnostics\ar-dynamic-evidence-review-20261026`. The source review worktree remains unchanged. No production logic, active configuration, renderer lifecycle or deployment was changed by the review.

## Boundaries and related stories

- VP-0035 and VP-0189 define responsive/stable mixed-aspect behavior; their historical accepted results remain historical evidence.
- VP-0136 retains the unresolved transient inward-overlay case. Do not resurrect the reverted blanket eight-second dwell or silently close this story.
- VP-0165 supplies lookahead performance and identity constraints; this design does not add queue latency.
- VP-0201 covers startup framing; VP-0213/VP-0214 cover presentation ownership and must be regression controls, not overwritten by a generic score.

Pixel-identical authored scenery and encoded mattes are not semantically distinguishable from pixels alone. Conflicting evidence must remain explicit uncertainty. The design's safety claim is bounded and testable; universal perfection is not established by review or a synthetic threshold sweep.
