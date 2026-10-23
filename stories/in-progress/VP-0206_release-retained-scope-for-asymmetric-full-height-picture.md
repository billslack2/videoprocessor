# VP-0206: Release retained scope for asymmetric full height picture

## Status

In Progress

Implementation `c48eb134c34a93c762e026af6daf50aaeb1dc83c` is published in separate draft unmerged [PR #194](https://github.com/billslack2/videoprocessor/pull/194), branch `codex/asymmetric-picture-scope-release-20261023`, worktree `E:\codex\videoprocessor\asymmetric-picture-scope-release-20261023`, based on freshly fetched `v1.4.02-beta` at `f7c7a8204f7a2cace68b3b40774406680af6c0d8`. Clean integration `4d1d62d0a664ba97137ba89497888bc8fad483a0` preserves installed24e and passes the full x64 Release Rebuild and dedicated native capture regression. The full integrated native suite, controlled measurement and deployment remain pending; no final regression count, deployment or user acceptance is claimed.

Controlled replay now confirms that the content-derived black cutoff directly causes the captured provisional geometry: cutoff 85 reproduces left bound 936, while isolated cutoffs 24/32 produce full-raster classification with full-extent support on both axes. This establishes the threshold cause for this frame, not an accepted global cutoff or completed presentation fix. The floor/cutoff math dates to `99addebdac523a648c918c311e3d81323f4110e1` on 2026-07-28 and is unchanged between deployments `e1996d67` and `24e0dd10`. The retained-scope policy and transition model are also byte-identical between beta `f7c7a820` and captured integration `24e0dd10`; this mechanism predates PR193.

## Update and deployment authorization

On 2026-10-23 the user authorized updating and deploying the correction. Story remains In Progress while root completes integrated regressions and deployment of the committed qualified-reference implementation. The exact-source x64 Release rebuild is complete; no new deployment is claimed by this authorization update.

Read-only preflight verified all three deployed runtime hashes and three Release/x64 sidecars against `24e0dd10`. PID68268 remains active with both renderer variants loaded; Config73936 and updater71340 are running. The prior deployment transaction refuses an active player and requires a fresh updater gate, all-target exclusive handles, verified current backups, rollback and final payload/config hash checks. The script must be repinned to the new clean tested integration rather than reused with its old24e receipt.

Active VideoProcessor.cfg changed since the prior receipt; its current hash is `5A744303135CFB76315BCE58DCDC0ED704B695B5E0CC904D5CF06A345170F97E`. Preserve current user configuration and dependencies. Do not restore the old DE93 configuration or replace it with source examples. Preflight did not signal/stop/start any process, acquire deployment locks, capture pixels or modify runtime/configuration. Root handles the authorized lifecycle and deployment after validation.

[Authorization preflight and integration preservation evidence](../assets/VP-0206/deployment-preflight-20261023/README.md).

## Objective

Release an old scope presentation when current native evidence establishes genuine full-height picture despite asymmetric dark side scenery, including paused or seek-like repeated content. Preserve the detector's native authority boundary, safe subtitle and near-black behavior, immutable frame identity and bounded render-thread work.

## Reproduced incident

The user paused and rewound after a scope scene. On process 68268, host and renderer clean `24e0dd10430e5140c06c1fa1bd106c9bbde61ef7`, generation 2 accepted scope `0,274-3840,1886` at 12:14:54. At 12:14:56 current evidence became provisional `936,0-3840,2160`; the displayed crop remained `26,274-3814,1886`. Recovery stayed at `0/7` with gates `observation-unavailable-or-not-contained`, `unsafe-excluded-bands`, and `presentation-owner-unresolved`, unchanged beyond five minutes.

The current left proposal is 936 pixels, floor 61, p90 76 and 99.31% dark; its absent opposite bar rejects horizontal authority. Excluded top and bottom p90 values 425/496 and visible-extent witnesses at 0/2160 contradict retaining the old scope bands. Broad outward confirmation reaches 3/3, but full presentation authority remains absent. Side aperture `0x0` and unevaluated vertical profile are missing proof, not completed negative inspections.

In 60 emitted windows covering 120.078 seconds, 2,878 of 2,879 previews completed under the sampled unknown-phase 1 ms allowance, with no optional deadline/source/display-period overrun. There were 2,875 invalid outward outcome events, no late/ready/publication/adoption events, and 922 completed proof-worker jobs. Outcome consumptions and unique jobs are different units. This incident does not support raising a global time budget as its repair.

The native capture contains 64 files for 32 unique accepted identities 10694-10728, with omissions and repeats recorded explicitly. Six full blobs sampled across five distinct identities plus one repeated identity are byte-identical. Pixel coverage is approximately 1.418 seconds before the near-2-GiB cap; the separate 14,031-record diagnostic trace spans 29.989 seconds. Format is `DecodedDolbyVision422`, R210 metadata, BT.2020, Full range. Source identity progresses while paused content stays unchanged in those samples.

[Compact incident and capture evidence](../assets/VP-0206/native-paused-scope-20261023/README.md). Native pixels remain only in the local capture; no binary frame or full live log is committed.

## Root cause and threshold investigation

The submitted-scope fallback requires independently confirmed presentation authority before creating a new envelope. Its current-sampling extension also requires safe excluded bands and a contained observation within one scan step; both fail here. The logical model refuses a provisional candidate irrespective of repeated observations. Elapsed time alone does not authorize recovery, so the same veto can persist indefinitely.

The native replay confirms that perimeter-derived floor math treats dark scenery as a black reference. The existing rule uses zero below code 32; otherwise it clamps perimeter p10 to 48-80 and adds 24, capped at 104, without range-specific treatment. Current left perimeter values 59-67 and deeper p90 76 yield floor 61/cutoff 85. In the exact same decoded-DV domain, true scope at 12:13:55 sequence 2620 had 276-row top/bottom bars with floor, p90 and dispersion all zero. This comparison supports a picture-derived reference error rather than a new global signal pedestal.

The unmodified production baseline and isolated scratch override agree at cutoff 85: provisional `936,0-3840,2160`, top depth mask `[0,48,63,63]`, bottom `[0,63,63,63]`. Cutoffs 24/32 yield `FULL_RASTER_TRUSTED` and `FULL_EXTENT_SUPPORTED` on both axes; cutoff 48 yields full-raster class with UNKNOWN axes, which is weaker evidence. Threshold-only tests leave floor metadata unchanged; separate threshold-and-floor tests isolate that variable. The three-frame matrix checks stored first/mid/last source pixels. These results do not establish final renderer adoption or safe behavior across other scenes.

[Controlled sensitivity results and reproducible nonpixel probe](../assets/VP-0206/threshold-probe-20261023/README.md) preserve the threshold JSON/CSV, native distributions, production baseline, scratch source and provenance. No arbitrary global threshold or production fix is accepted. An independently qualified matte reference remains a candidate and must carry provenance/uncertainty. Failure of a strict flat opposing pair is not evidence of full raster: a genuine raised-64 matte with caption contamination above 10% of an outer edge can fail that test. Require raised-40/64, caption/UI, noise, depth and axis-local negative coverage before any production correction.

## Implementation and integrated validation progress

Full-range evidence qualifies raised mattes from opposing spatial/depth support and preserves unresolved noise rather than treating failed flat-pair qualification as full-raster proof. Nominal full-raster classification requires affirmative distributed support on both axes. Calibration reuses existing edge/outer-line samples and is shared with retention. Exact supplied/cached evidence avoids duplicate reference scans; refined/external geometry without calibration explicitly rebuilds the current source reference, retaining its geometry and cooperative budget/cancellation guards.

Black-reference telemetry reports observed/effective floor and cutoff, qualified/unresolved edges and axes, range and exact source/domain/policy/continuity, candidate versus conditional published bounds, vote counts and cadence repeats. Normal logging shares the existing decision throttle and performs no pixel inspection.

The preserved-runtime integration is `4d1d62d0a664ba97137ba89497888bc8fad483a0`, published on `codex/vp-0206-deploy-20261023` in `E:\codex\videoprocessor\vp-0206-deploy-20261023`. Independent review verifies exactly eight intended changed files relative to24e and23 retained file/tree identities, including the entire GUI, logger, NVIDIA, fullscreen/readiness, prepared workers/caches, budgets and GPU confidence. Four project/filter inventories preserve installed/feature entries without duplication;14 new black-reference tests are registered once. An integration-only regression checks refined geometry missing calibration, exact sample counts and cancellation without partial publication.

The exact clean integration completed a full serial x64 Release solution Rebuild with exit0. All three runtime output hashes, genuine Release/x64 sidecars and embedded exact commit identities are verified. The dedicated integrated capture regression passed1/1:64 original files,32 distinct identities,32 repeats and two gap events; full presentation releases on10694 and logical full geometry publishes on10700. Synthetic eight-source Scope prehistory is explicit, original frame identity is unchanged, source-duration timing and production authority/submission reducers are used, and failed-submit anchor state is retained. Raw extraction mean0.251122ms was collected under concurrent native-suite load and is not a controlled performance result.

Preliminary feature validation passed14/14 isolated black-reference tests and6/6 capture probes. The full integrated native suite is still running with no final count; the controlled benchmark waits for that load to finish. Deployment has not occurred and running player68268 remains on24e. Raised40/64 bars, caption/UI, bounded noise, depth and axis-local negative coverage remain part of the acceptance boundary; no arbitrary global cutoff or strict-pair-failure promotion is accepted.

[Integrated build identity, source review and dedicated capture result](../assets/VP-0206/implementation-4d1d62d0-20261023/README.md). Final native results and the deployment receipt must be recorded before Review; live acceptance remains a separate reported validation item.

## Readiness review

The user AGENTS.md selects the discovered current beta and overrides the older index confirmation prompt. Root verified current beta `f7c7a820` and created the clean source worktree above. This story is separate from VP-0205 delivery/performance work and broader VP-0124 lookahead. Exact native evidence and production replay helpers are available; immutable source identity, current base/owner/policy validation and render-owned authority remain explicit boundaries. No configuration or UI migration is needed. The selected qualified-reference implementation is committed; remaining actions are final integrated native regressions, controlled measurement, authorized deployment and user playback acceptance.

## Design boundaries

- Native threshold/math causality is reproduced. Qualify any matte reference independently; lack of a flat pair must never imply full-raster authority. A current, native vertical outward presentation proof that preserves every source column remains a candidate design, not a predetermined exclusive fix.
- Any presentation certificate must bind the exact source, domain, generations, continuity, base, owner and policy; reject stale, incomplete or incompatible results. Keep publication and authority mutation on the render thread, with bounded worker or cache reuse where substantial inspection is needed.
- Do not convert repeated provisional observations, elapsed timeout, speculative full-raster FIT or a dark asymmetric side into native crop authority. Do not transfer votes to inward cropping, mixed-axis cropping, subtitles or NLS.
- Preserve supported true black bars, raised Full-range black, near-black scenes, sparse titles, subtitles and overlays. Current picture evidence may reveal supported pixels only after the required independent proof.
- Add aggregate proof/rejection telemetry with reasons, exact context and spatial support. Avoid per-frame formatting or new blocking render-thread scans.

## Acceptance and validation

1. Reproduce the captured retained-scope failure with the production native sampler/policy, respecting duplicate/gap identities. Establish the pre-fix behavior and the correct interpreted format/range.
2. Isolate black-level/range threshold effects. Cover Full-range zero and raised bars, Limited range, asymmetric dark scenery, gradients and both horizontal orientations; show that any threshold change preserves genuine bar authority and does not invent Full-range brightness assumptions.
3. Acquire a trusted scope base, then repeatedly present the captured full-height asymmetric scene. A current independently proven outward presentation releases the invalid retained top/bottom crop without promoting the raw provisional candidate to native authority. Repeats, new source IDs with identical pixels and seek/discontinuity behavior are independently covered.
4. Reject sparse subtitles, titles, overlays, near-black, contradictory bands, stale identities, wrong bases/owners, changed policy and incomplete proof. Do not manufacture opposing support by combining different frames.
5. Record reasoned proof request, evaluated aperture/support, accepted/rejected axes, completion, publication/adoption and effective source lead. Distinguish cached invalid/no-change, missing evidence and deadline cancellation.
6. Pass relevant native AR, source-crop, subtitle/near-black, ownership and lookahead regressions and a completed x64 Release build. Report measured foreground/worker cost and live validation separately; deploy only after user authorization, preserving configuration and recording paired binary hashes.

## Tracker audit

Before allocation, fetched and fast-forwarded clean tracker main `ae7f40684630f6f2ae2b90112f9b38be77af03d9`. All 224 canonical files and 224 index rows matched, including 203 roots and 21 children; maximum root VP-0205, registry and child sequences agreed. No duplicate, missing, orphan, state-folder/index or count conflict was found. Assigned VP-0206 and updated the registry to next VP-0207/225 items atomically with this In Progress record.

Historical narrative status formats, VP-0070's missing `## Status` heading and VP-0168's index capitalization are recorded without unrelated repair. The new story uses the exact current state spelling.
