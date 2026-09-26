> Superseded in part by [Verified vertical crop review](VERIFIED_VERTICAL_CROP_REVIEW.md): the three-bright-zone prerequisite described below did not resolve the paused Eternals frame. Strict independently verified vertical removal now does not require side brightness. The raw-aperture binding fix remains.

# Current-evidence-first crop review — 2026-09-25

Status: the first boundary correction passed a clean x64 Release solution build, 67 focused tests and all 1626 full regression tests. The raw-witness follow-up below is under validation. User/architect prefers a clearer current-evidence route when sufficient; learned geometry is a secondary path.

## Finding and scope

Paused source log: artifacts/overnight-framing-20260925/paused-side-conflict-0733.log. Applied geometry was 0,68–3840,2092 (1.8972). Independently trusted vertical geometry was 0,276–3840,1884 (2.3881); both bars were flat Y64. Horizontal scanning proposed a 372-pixel right inset but rejected its edge. Actual left-side samples had 8–12/12 strong pixels in the same three height zones at each of three depths; the fourth zone had only 2–3 strong pixels but 11/12 nonblack pixels. Requiring six strong samples in every zone blocked this evidence.

Two independently confirmed native scenes suffice to qualify a learned format. Here both vertical boundaries are already observed, so requiring history adds reset/cold-start failures without supplying a missing coordinate. The separate learned path for an unobserved boundary must not inherit this exception.

## Current native side certificate

Shared ExtractActivePictureEvidence / SupportsVerticalCropDespiteSideAmbiguity keeps the original all-four-strong-zones rule unchanged. The additional mode requires:

1. Independently trusted current top and bottom boundaries, complete relevant scans, full-width target and rejected-horizontal-bar context. No trusted horizontal crop may be overridden.
2. Positive picture probes only at an actual source edge the raw scan leaves uncropped. Diagnostic measurements of a proposed inset side cannot become authority.
3. On one side, the same three of four height zones contain at least 6/12 strong samples at every one of three strip depths. No pooling between different zones or opposing sides.
4. Every zone at every depth has at least 6/12 nonblack samples. One shadowed region is allowed; an entirely black zone is insufficient.
5. Strict current Y/U/V source-profile checks of both exact excluded bands. Missing/failed sampling grants nothing; generic black-cutoff or remembered brightness cannot replace these checks. Ordinary retention and caption guards remain.
6. Preserve raw native measurements and full width. Retain at most one native scan step of boundary uncertainty in the visible aperture, strictly recheck the remaining excluded bands, and bind same-frame side witnesses explicitly to this outward-only envelope.

Keep real minima, masks, nonblack counts, sample geometry and guard amounts in axis evidence. Never raise old minima to pretend all four zones passed. Authority-bearing fields participate in equality for queued observation identity. No hardcoded aspect ratio, global threshold, confidence percentage, timer or crop interpolation was added.

## Timing and lookahead

Use the existing native two-frame clear-transition confirmation, shared by live and queued extraction. Current side permission grants only the verified vertical crop; the failed horizontal measurement remains visible. Keep lookahead bounded by actual available frames, source identity, continuity, exact geometry, near-black checks, moving-boundary holds, captions and current-frame adoption. An eligible queued run may authorize its first supported frame, never an earlier contradictory frame. Insufficient queue uses live confirmation; there is no wait to fill the queue.

No inferred origin is admitted through the native scheduling path. A future learned lookahead design would need its own identity-bound proof rather than borrowing this current-only certificate.

## Test gates

- RED source fixture for the measured three-strong/one-shadowed side; GREEN publishes native scope with no history.
- Mirrored sides and shadowed zones, multiple resolutions, P010/P210/native v210 and chroma alignment.
- Preserve negatives for one/two strong zones, black fourth zone, depth/zone pooling, thin border, localized logo, sparse credits, interior-only support, missing samples and wrong aperture.
- Preserve uniform tinted/raised inset and genuine windowbox/side-bar negatives, including prior known aspect. Do not remove negative assertions to obtain GREEN.
- Reject picture/chroma/caption intrusion in an excluded band, including later confirmation frames. Preserve ordinary caption and crop-retention logic.
- Queue proof: sufficient actual lead applies exact inward crop on the first eligible frame; contradictory, missing, stale or repeated evidence breaks proof. Configured zero and insufficient lead retain live timing.
- Regressions for measurement jitter, IMAX return/expansion, gradual Mandalorian transitions, full-raster releases, source generation and ownership changes.
- Full x64 Release tests and independent review before deployment. Synthetic distributions are not a substitute for actual playback validation.

## Telemetry and reviews

Log original minima, zone masks, nonblack minima, sampled side/aperture, strict profile outcome and admission reason. Distinguish current-source proof from remembered inference. A nomination log with history=0/native_scenes=0 is not necessarily an empty registry. Renderer/source resets can clear history; do not call every renderer recreation an HDMI resync.

Independent reviewers: learned_edges_design, learned_edges_qa, available_lookahead_final_review. All endorsed the narrow current-only side certificate and rejected a blanket failed-axis override. Sparse sampling remains an engineering heuristic, not proof of authored intent.

## Boundary correction from the earlier replay

The first deployment still rejected all 14 strict-profile mismatches in rows 275 and 1884–1886. Remaining sampled bars matched Y64/U512/V512; no deep mismatch existed. The native row step is four. A source regression failed on the then-deployed policy while 62 other focused tests passed.

The correction retains one native scan step at each boundary only in the added shadowed-side route, leaving uncertain pixels visible. The original profile must complete and be clean or fail only within that step; remaining excluded bands must pass unchanged strict limits. Padding is deterministic within this route. The side distribution was initially remeasured over the resulting aperture; the follow-up below replaces that unnecessary resampling with explicit raw-witness binding.

History normalization cannot substitute an aperture the current certificate does not authorize. CommitCandidate already chooses current authoritative coordinates instead of an old candidate anchor. Mixed native/guarded apertures fail exact queued proof safely and use live confirmation. Guarded coordinates cannot train the independent exact learned-edge registry; ordinary native formats still can. Fuzzy history matching is outside this correction.


## Raw side witness correction — September 25 replay

The bounded per-frame trace reproduced the same delay twice: generation 2 sequences 721–737 and 1922–1938. Native top/bottom boundaries were 276/1884 on the first frame; lookahead adopted the guarded 272/1888 crop 16 frames later (about 667 ms at 23.976 fps). Most of that delay was insufficient side-picture support: actual side samples began near Y80/Y78 against Y64 bars, below the existing Y88 nonblack threshold. Both independent reviews found that accepting remembered geometry plus clean top/bottom bars alone would also accept the existing deliberately raised/tinted inset counterexamples. This change does not introduce that bypass or lower brightness thresholds.

A separate two-frame interruption is demonstrated at sequence 736 (repeated at 1937): the raw three-zone side witness passes, and the final guarded excluded bars pass with zero mismatches, but changing the vertical aperture by four pixels moves every side sampling point. The shifted grid misses enough bright samples to invalidate the proof. The added rows are retained pixels, so they do not require positive picture support. Preserve the current raw witness and bind it explicitly:

- sample top = final top + top guard;
- sample bottom = final bottom − bottom guard;
- final aperture contains the nonempty witness, guards are nonnegative and at most one native scan step;
- unchanged side-distribution thresholds and strict source profile of the final excluded bars;
- exact current source/frame identity, queue proof and geometry normalization checks remain mandatory.

This is reuse within the same source frame, not temporal carryover. Guarded observations remain excluded from exact native geometry learning. The expected improvement in the recorded sequence is approximately two frames (83 ms); it does not establish that the earlier dark frames are safe to accept.

A new source fixture reproduces the sampling phase change across P010/P210/native v210. RED: x64 Release build passed, 67 focused tests passed and only `RawSideWitnessSurvivesOutwardGuardSamplingPhaseChange` failed. GREEN and deployment results are recorded in `artifacts/raw-side-witness-20260925` after completion.

### Targeted source diagnostics

Startup-only `VP_SIDE_PIXEL_TRACE_FRAMES=N` (1..600; default off) logs distinct source snapshots while both vertical boundaries are trusted, the horizontal axis is rejected, and the candidate is materially inward from the current geometry. It reads six actual-side strips (three depths per side, 48 samples each) and four 96-sample rows immediately inside/outside the native top/bottom boundary. Each snapshot includes normalized Y/U/V, exact coordinate rules, source generation, sequence, renderer instance, format, candidate and retained bounds. It reads the currently displayed source frame, not queued or stale evidence, and supplies no detection authority.

The budget bounds emitted snapshots to N and sampling attempts to 10*N; identical measurements are not relogged, repeated source sequence numbers are skipped, and exceptions disable this diagnostic. Completion lines include sampling/formatting/logging time so its overhead can be measured. Source facts from this trace must precede any proposed texture/color threshold change. The older three-second periodic grids missed the start of this subsecond transition.

Final follow-up validation: clean x64 Release build, all 69 focused and 1628 full tests passed; deployed and verified at 09:03–09:04 EDT. See artifacts/raw-side-witness-20260925/deployment-20260925-090326.json and running-verification.json. The exact problematic replay has not yet been verified on this build.
