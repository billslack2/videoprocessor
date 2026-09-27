# Learned-aspect eligibility and protected history â€” 2026-09-27

## Scope

Continue the user-approved current local test build in `codex/partial-retention-proof-20260926`, base `ab683eefe5fdffa018b788eedfcd8913dbebe1b4`, preserving earlier tested fixes. This change limits only the secondary learned fallback. Primary active-picture detection, including 4:3 and nonstandard formats, remains unrestricted. No crop snapping, detector threshold, deadband, confirmation timing, lookahead, queue or subtitle behavior is changed.

## Implemented policy

Eligible learned ratios: 1.85, 1.90, 2.00, 2.20, 2.35, 2.39, 2.40, 2.55, 2.76, with inclusive relative tolerance of 1% from any listed value. Eligibility uses the measured rectangle dimensions, not cached aspect metadata. Scope-family intervals can overlap; the list is an eligibility filter, not nine disjoint clusters. Exact measured rectangles remain the history keys and crop targets. Qualification, nomination, and remembered-proof validation enforce the same guard. Existing full-width TOP_BOTTOM restrictions remain.

The history still has three slots. Empty or expired slots are reused first, then unqualified entries. When all three are qualified, one bounded pending candidate accumulates the existing proof: three consecutive native observations per confirmed scene, in two distinct scenes. That pending candidate cannot nominate a crop. Only after qualification may it replace the least-recent qualified entry. Promotion transfers its own ID/revision; no history ID is reused. Different nearby rectangles cannot pool credit. Known-entry updates do not erase pending proof, but frame gaps interrupt consecutive-sample credit. Source/backward discontinuities and expiry clear pending evidence. A presentation reset discards the overflow candidate while preserving the previous source-bound history behavior.

Startup telemetry states the eligible ratios, tolerance and capacities. Guarded eligibility telemetry includes overflow_pending so diagnostics can distinguish protected history from a waiting fourth candidate.

## Pause and OSD findings

No static-image rejection heuristic is enabled by this change. Three new characterization tests run real SceneDetector and native P010 extraction into the history model with advancing HDMI source sequences and cadenceRepeat=false. Unchanging startup imagery cannot manufacture scene IDs. One real cut followed by repeated identical captures can earn one scene credit. Broad in-picture OSD appearance/removal can look like two cuts and qualify a common geometry. The fixtures explicitly wait for synthetic presentation ownership to settle; they are not full renderer playback tests or a reliable Apple TV pause detector.

A known ratio alone does not prove movie content or exclude OSD. Current pixel-profile verification, native authority requirements, source/age constraints, ambiguity rejection and the normal admission path remain required. A future static-image learning filter needs evidence around noise/dither and slow movement; it should suppress new learning credit without disabling normal paused detection or use of existing qualified history.

## Failing-first proof and review

Sources/tests before this change are preserved under `artifacts/learned-ratio-eligibility/before`. The initial clean x64 Release test run (`artifacts/partial-retention-proof/learned-eligibility-red-clean/tests.trx`) ran nine groups: three expected failures demonstrated non-common learning, missing 1% boundaries, and eviction of a qualified entry by an unqualified fourth candidate (qualified count 3 to 2). Six characterization/preservation tests passed. Two additional groups cover exact-coordinate isolation under alternating overflow candidates and both native/full-frame and embedded/pillarboxed 4:3.

Reviewers independently checked architecture, test propositions, cache promotion/expiry, primary-path isolation, and telemetry argument ordering. No blocking issue remained. Existing assertions were not weakened. The incremental RED link hit LNK1103; only the subsequent successful clean build/test run is used as RED evidence.

Final validation: successful clean x64 Release build, 11/11 focused groups passed, and 1,799/1,799 full-suite tests passed on the first full run. No existing nonblank test lines/assertions were removed from either touched test file. `git diff --check` passed. Exact source and runtime-pair hashes are recorded in `artifacts/learned-ratio-eligibility/validated-build.json`.

No deployment, restart, configuration edit, merge, or package was performed. Deployment files still match the preceding runtime pair. This prepared build also includes the previously tested presentation-envelope lifetime corrections.

Next manual test: use normal launch after deployment, play strong scope/IMAX scenes to establish history, then replay the difficult Eternals return and TS5 near-1.85 sequence. Check common_aspects/aspect_tolerance_percent startup policy, qualified counts, overflow_pending, current pixel rejection, and final framing changes. Existing learned pixel checks and normal crop admission remain the final authority; whitelist membership alone never authorizes a crop.
