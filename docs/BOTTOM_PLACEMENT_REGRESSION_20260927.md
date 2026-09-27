# Logged bottom-placement / optional-fill regressions

Three new tests in AlphaSourceCropPolicyTests.cpp exercise production crop policy (where applicable), optional fill, source-coordinate aspect resolution, and final bottom-aligned FitAspect layout. Fixtures are decision snapshots from vp.log.3.txt, not captured pixel frames or a full temporal/render replay.

Expected fixed positions on a 3840x2160 bottom-aligned 16:9 screen:
- 3906/3911: envelope y94..2052, logical y176..1980 -> picture y202..2160.
- 3908: envelope/logical y176..1980 -> picture y356..2160.
- 6276: envelope y108..1996, logical y168..1992 -> picture y272..2160.
- 6290: envelope y96..2042, logical y168..1992 -> picture y214..2160.

All log-derived cases require no optional fill and preserve the full source width. Changes to the top position from safe envelope expansion remain permitted; bottom alignment does not freeze height. The positive control uses eligible logical aspect 1.92 with a 3840x2070 envelope, producing a 3680x2070 exact 16:9 fill and full-screen placement.

## RED proof

Temporarily disabled only `if (input.contentReferenceAvailable)` in EvaluateAspectLimitFill, recreating the old envelope-only policy. Existing production code was preserved byte-for-byte in bottom-placement-policy.original. Clean x64 Release build succeeded. Of the three new tests, both bug regressions failed and the eligible-fill control passed:
- sequence3906: expected picture.top202, actual0.
- sequence6276: top272, fill0 (correct control snapshot).
- sequence6290: expected picture.top214, actual0, fill1.

Results: red-bottom-placement/tests.trx and tests.log. This is a controlled old-policy mutation of current code, not a claim to have rebuilt the entire historical78a1f61 version.

The original policy was restored byte-for-byte and git diff confirmed no remaining production change to AlphaSourceCropPolicy.cpp before the GREEN rebuild. No RED binaries were deployed.

## Independent review

Reviewer checked numeric geometry, current renderer reference binding, fixture admission, and final source selection. No blocking concern. Renderer passes current-generation logical geometry as the content reference and vetoes missing/stale references. Tests do not instantiate the renderer or reconstruct its exact translation-confirm state at6276, and do not prove whether initial bars were real. The test fixtures exercise the relevant admitted rectangle and fill/layout policy.

## GREEN validation
Restored production policy, clean x64 Release rebuild: all1836/1836 tests passed, including all three new layout regressions. Results: green-bottom-placement/tests.trx. The only new source changes for this follow-up are tests; production fill code is unchanged. No deployment or package update was made.
