# Learned aspect-family replacement

This continues the user-approved local test build in `codex/partial-retention-proof-20260926`, retaining all earlier tested changes. It supersedes exact-coordinate-only cache management in LEARNED_ASPECT_ELIGIBILITY_REVIEW.md; the existing eligible ratio centers and +/-1% admission windows remain unchanged.

## Policy

Six learned families: 1.85/1.90; 2.00; 2.20; 2.35/2.39/2.40; 2.55; 2.76. At most three families qualify at once. Each has one exact measured representative, never an average or a snapped nominal rectangle. One global pending candidate stages a same-family correction or a newcomer to a full cache. Competing exact candidates cannot pool evidence. A replacement must independently earn three consecutive native frames in each of two confirmed scenes.

Learning can correct a retained native representative within the same family without changing current stable presentation. The broader relationship requires a real height change, finite cached aspects consistent with rectangle dimensions, and center displacement within the existing native geometry tolerance. The previous SameBounds path remains intact. The new relationship is learning-only: primary Observe, pixel scanning, deadbands, subtitles, lookahead, and rendering admission remain unchanged. A replaced representative becomes eligible only in a later confirmed scene. Promotion uses a new history ID, invalidating certificates for the retired representative.

Three bounded retired-coordinate witnesses per family can only veto ambiguous recall, never nominate a crop or provide qualification credit. A shared top with a different bottom (or the converse) remains ambiguous, including A -> B -> C replacements. Exact repeated witnesses are refreshed rather than duplicated. On loss of a still-fresh witness due to capacity, recall abstains until that lost history expires under the existing 10-minute/64-scene limits. Source reset and normal history expiry clear these witnesses. Updating a representative does not refresh the age of other retired witnesses.

No persistent learned state or configuration change is introduced. Primary 4:3/nonstandard detection is unchanged. Existing strong-pixel checks still decide whether a nominated remembered rectangle can be used. OSD changes can resemble scene changes; this change is not semantic menu or pause recognition.

## Validation

Initial clean x64 Release RED: four new RememberedFamily tests all failed on the preceding implementation. Two failures show learning blocked while the stable rectangle is retained; two show multiple qualified entries for one family. Further review and final test results are recorded with the build receipt under artifacts/learned-family-replacement.

No deployment, restart, package, or merge is part of this change.

## Final results

The first implementation review run passed 8/11 groups and reproduced two defects: 9-pixel translated geometry could bypass the original learning relationship, and repeated A/B retirements filled witness slots with duplicates. The third failure was the deliberately changed cache cardinality (two exact provisional entries became one family slot). Only that existing count assertion changed from 2 to 1; its zero qualification, zero scene credit, no nomination, and unchanged presentation assertions remain intact. Reviewers approved this narrow test-policy change.

A further capacity test reproduced false overflow for A/B/C/D/A. Removing a retired witness when that exact geometry becomes active resolves it without losing alternative-edge knowledge. That test failed before the correction and passed afterward.

Final x64 Release build succeeded. All 22 focused tests and all 1,809 full-suite tests passed. Ten new regression groups cover independently qualified future-only corrections in both grouped families, alternating candidates, incomplete/experimental evidence, retired A/B/C ambiguity, bounded overflow and expiry, repeated A/B and A/B/C/D/A cycles, source reset, and current pixel proofs carrying retired history identity. The full suite includes existing lookahead, guarded-return, pixel-format, subtitle, native crop, and presentation tests. Two independent code reviews found no remaining blockers after the corrections. git diff --check passed.

The only removed nonblank line in the preexisting tests was the reviewed 2-to-1 cache-count assertion. Hash comparison to the prior validated receipt confirms only the model cpp/header, renderer startup telemetry, and remembered-history test file changed; previous detector/presentation fixes remain intact. Exact hashes and RED/GREEN records are in artifacts/learned-family-replacement/validated-build.json; this-change.patch preserves the scoped delta.

This is automated validation, not a new HDMI playback result. The runtime pair in C:/Videoprocessor/vp is unchanged. The next manual check, after an explicitly requested deployment, is repeated scope/IMAX/Eternals transitions and near-1.85/1.90 content, confirming stable displayed framing while learning refines future recall. A current-source pixel veto still takes precedence over learned history.
