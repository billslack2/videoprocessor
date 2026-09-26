# Verified vertical crop: deep review, 25 September 2026

## Demonstrated defect

The paused Eternals frame has independently trusted top/bottom edges at 276/1884 in a 3840x2160 source. The established crop remains 0,68-3840,2092. Horizontal scanning proposes a dim right margin and rejects it (`BAR_EDGE_REJECTED`). Full-width vertical cropping would leave every side pixel visible.

The deployed 09:03 build nonetheless requires three bright side zones. Actual source samples at 09:10:00, sequence 505, renderer B009438B show two bright zones (mask 3) and a minimum 10/12 samples above the black cutoff across the left side. This frame cannot pass that quorum even when paused indefinitely; additional confirmation frames cannot help.

This exposed a policy conflict: the extractor grants vertical-only authority, but failed-axis retention can veto that same crop because horizontal evidence is incomplete. A second partial-composition rule can also veto earlier frames with two dim margins. Such content can acquire at startup and remain blocked after an IMAX-format scene. The previous raw-witness fix addressed a separate two-frame sample-grid interruption, not this persistent veto.

## Correction and explicit tradeoff

Preserve the original four-strong-zone route. For incomplete side evidence, permit only the independently verified top/bottom crop when strict, current Y/U/V inspection supports the bands being removed. Keep full source width and keep horizontal evidence marked failed. Do not infer an AR or promote uncertain sides to picture authority.

The strict proof is bound to the exact raster, raw aperture, and final target. A bounded outward guard keeps one native scan step of boundary uncertainty visible. Both live and queued paths use the shared proof. It bypasses only the incomplete-axis/partial-composition vetoes; it does not bypass native vertical trust, occupied excluded bands, near-black acquisition protection, caption/presentation checks, moving-boundary holds, normal confirmation, source identity, or queue continuity.

This intentionally changes the dim-inset policy: dim authored side padding stays visible while verified top/bottom bars may be removed. Intent cannot reliably be inferred from dim side pixels alone. Tests that required indefinite retention solely for that intent assumption are changed explicitly. Confirmed four-sided windowbox protection remains. User-enabled fill/NLS may subsequently transform an accepted crop according to existing settings; those are separate from source-crop authority.

## Review and cleanup

- Fresh architecture review found the per-axis conflict and implemented the shared correction.
- Independent pixel/QA review reproduced the logged two-zone failure and designed removed-pixel negatives.
- Independent caption/presentation review added NLS and fill contract tests and is reviewing the final patch.
- Removed obsolete three-zone shadowed-side admission helpers; renamed profile/guard metadata to describe verified vertical removal.
- Retained side measurements for diagnostics and the original four-zone route. They are not dead code.
- Dormant remembered-edge and sparse experiments remain disabled for this deployment. They are not required by this correction; broad deletion of those experiments is outside this focused validation.

## Evidence and tests

RED: successful x64 Release build; 73 focused tests, 71 passed and exactly two new tests failed. The recorded-side fixture reached mask 3/nonblack minimum 10 before rejecting the crop. The second fixture failed for dim side compositions across source formats. All 18 prior DimSide expectations passed unchanged in RED.

The recorded fixture contains the actual 144 sampled side Y/U/V triples; unsampled areas are synthetic and explicitly identified. It is not a captured full-frame/video replay.

GREEN coverage includes P010, P210, native v210; retained side pixels; exact/outward guard binding; deep occupied Y/U/V/overlay bands; missing vertical proof; genuine windowboxes; near-black; captions; ramps; source/queue identity; remembered geometry; and three downstream NLS/fill tests. Final counts and deployment identity will be appended after completed validation.

## Limits and next playback

Bounded sampling is not a proof about every pixel. The strict profile is conservative about any sampled content in excluded bands. It now runs more frequently when sides are dim; runtime cost must be checked. No brightness thresholds, confirmation counts, or lookahead depth were changed.

Restart acquisition on an already paused scene does not prove transition timing is fixed. Replay the original HDMI movie from the prior IMAX scene through this scope scene. Verify full-width vertical publication without an intermediate full-raster bounce, then inspect normal viewing, captions, and gradual format changes.

## Measured scan cost

An optimized standalone x64 benchmark linked the actual Release library (10 warmup +100 measured extractions per fixture/format). The paused-style strict route measured median/p95: P010 0.466/0.605 ms; P210 0.456/0.511 ms; native v210 0.561/0.706 ms. The original bright-side route measured medians 0.124/0.117/0.154 ms respectively. Both-dim strict fixtures remained below 0.8 ms p95 across these formats. A large-bar stress case reached native v210 median1.288/p951.744 ms.

The paused-style extraction uses 47,248 luma samples, including 33,600 additional strict-profile samples. These are per-extraction synthetic CPU measurements, not end-to-end rendering latency or a matched-input playback A/B test. Benchmark source, raw observations and linked-library identity are preserved under `microbenchmark/` in the validation artifact directory.

## Broader audit follow-ups

No additional missed inward veto was found across the live model, timeline, publication adoption, history binding, or buffered inward decision. The older outward gradual-motion helper still rejects any failed axis. That is a separate expansion policy, not a blocker for this inward transition; changing it requires dedicated dim-side outward/fade tests. Consolidating duplicate profile sampling or reusing extraction within one source frame may improve cost later. Neither is needed to validate this correction, and neither was changed opportunistically.

## Final validation and deployment

Two new tests were RED before production edits. Final validation: successful clean x64 Release solution build, clean test-target rebuild after explicitly reviewed policy/aperture expectation corrections, 76/76 focused and 1,636/1,636 full tests passed. The final test-field correction distinguishes a pending proposed rectangle from retained stable presentation; publication timing and horizontal-crop rejection assertions remain. Architecture, QA and caption/presentation reviewers found no blocking issue. Initial failures and all intermediate results are preserved.

Deployed 2026-09-25 09:44:37 EDT to C:\Videoprocessor\vp, process42280. Verified the exact executable path and loaded renderer at09:45:07. EXE SHA256 A7B7B67575C2394446ABD0638F79B70A4DF52C5E4C9731EC59FEDB7DC42C4D64; renderer D3F72628D261E9AE3EC67C09AB0706093BFA222943F7DEA721D5A318A04DF267. Config/state preserved; previous runtime pair backed up at deployment-backups\verified-vertical-crop-20260925-094437. Source remains the reviewed dirty experimental worktree at baseff53b468; receipts and source hashes record the exact build. Learned/sparse experiments remain disabled for this launch.

Playback moved after restart. At09:45:20, renderer E9C80A73, generation1, sequence829 published the new native verified vertical path: base0,68-3840,2092 directly to0,272-3840,1888. Both sides were uncertain; vertical_profile_support=1, blocking_failed_axis=0, strict guard mismatches0, max Y delta3 and UV delta1. The raw boundary had7 mismatches, all inside the outward-retained uncertainty rows. Scheduled publication was accepted, and final layout used the scope envelope directly without an intervening full-raster release. Existing configured fill then produced22,272-3818,1888; that downstream adjustment is separate from full-width detection.

This proves the new path executes with real HDMI input. It is not a matched-input before/after comparison or visual proof of perfect timing, and does not establish that sequence829 is exactly the previously paused frame. Rewind from the preceding IMAX scene and judge the visible transition. No further speculative threshold changes were made.
