# Cold native picture acquisition with caption occlusion

On the 2026-11-04 paused source, the source-picture producer nominated physical bounds275..1884 but its unmasked verifier encountered caption ink at1656,2007 (Y940 against matteY64). The existing caption-masked verifier could validate those pixels, but cold subtitle work had no accepted picture plane and did not inspect the available current nominee. Generic bar extraction failed on the same sources.

SubtitleMeasurementSampler now uses the exact current source's spatial nominee as an inspection-only plane when no raw scan ran. It retains the original request/shared snapshot, grants no bar or crop authority, and leaves completed negative scans unchanged. Its existing strict masked verifier must independently accept the resulting current raw glyph mask. Work remains on the bounded optional worker; no spatial scan, delay or render-thread pixel work was added.

Focused RED/GREEN: new cold scan/cache test,16 foreign/context or unavailable nominee cases, connected-picture/menu negatives and existing spatial acquisition/missing-source controls. GREEN7 receipt: artifacts/qualification/runs/20261104-081256-ee15437f/iteration.json. The first integration run caught source-format versus transport generation; the corrected gate preserves both established source conventions and exact full identity matching.

Ordered original-pixel replay, preserving available identities and gaps:
-0744capture91frames:67current nominee scans and strictproofs; model acquires atS432 and retains90/90 thereafter.
-0751capture89frames:68scans/proofs; model acquires firstsavedS182 and retains89/89.
-Measuredphysical275..1884 is2.3865755 (2.39 displayed); guarded sampling274..1888 is2.3791822 and is not the measured aspect.
-Current physical estimation still fails on24/21frames. The captured top-edge votes fragment across overlapping measured uncertainty; strongest bin31 versus32 required. That separate measurement issue remains open and is not concealed by model retention.

Sealed original-pixel evidence is at artifacts/paused-ar-20261104-0744/worker-ordered-replay/receipt.json. This change alone does not meet the user's all-phase current detection acceptance criterion. Actual-renderer recorded-source replay and subsequent uncertainty work remain required. No deployment recorded here.