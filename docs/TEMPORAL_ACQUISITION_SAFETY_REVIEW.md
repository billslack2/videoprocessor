# Temporal acquisition safety review — 2026-09-26

## Question

Can a temporally stable scope boundary plus clean current excluded bands authorize a never-presented crop when native extraction remains provisional?

The paired Alien replay establishes useful local boundary measurements: both paths nominated280..1879, and refinement did not create the first anchor earlier. In the second replay, final admission still reported reference=0, picture_supported=0, retention_safe=1 after a logical0,280–3840,1880 rectangle existed. Logical geometry is not an admitted displayed crop.

## Code review

The deployed shadow path deliberately does not participate in production. Existing sparse startup experiments are off and their one-shot source-generation gate closes after native logical publication. The transition experiment requires current complete/conflict-free side evidence and genuinely inward geometry, so it cannot be silently repurposed to admit the same never-presented logical rectangle with failed side scans.

Current retention already checks clean excluded bands plus a contained provisional proposal (or its documented near-black/unavailable alternatives). Rebranding that same evidence as acquisition adds no independent discrimination. Existing MovingArtworkCounterexampleRemainsAnExplicitAcquisitionLimitation demonstrates that moving artwork on intentional full-raster black can satisfy the temporal and current-exterior experiment too. This is an ambiguity, not a demonstrated admission plumbing bug.

## New integration coverage

Three TemporalAcquisition tests use actual4K moving pixels and current sampling. They cover no logical geometry; logical geometry that was never presented; later independently sampled native bars acquiring without a new source generation/epoch; and ambiguous moving frames retaining the exact contract only after genuine admission. The existing test expectations are preserved.

A deliberate isolated mutation disables only the final acquisition-versus-retention gate to test the tempting shortcut. This is not a proposed patch or deployable build. The expected negative tests must reject this mutation. Restore the production file byte-for-byte, then run the full Release suite before completing the work. The corrected mutation trial failed two intended admission assertions. After restoring production byte-for-byte, all 1,721 tests passed in x64 Release. Independent review found no blocker and supported preserving the guard. The tests exercise policy/admission recovery; they are not a pixel-exact movie reproduction or a full renderer-lifecycle test.

## Decision boundary

These tests cannot prove every future crop interpretation. They can demonstrate that the proposed shortcut admits a known counterexample and that the existing guard need not permanently block later real native evidence. Unless genuinely independent evidence is established, preserve production behavior and do not add another speculative diagnostic/deployment cycle. The user has explicitly preferred waiting over a generally worse detector.
