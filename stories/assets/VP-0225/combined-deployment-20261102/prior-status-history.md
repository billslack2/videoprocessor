## Status

Review

2026-11-02: Implementation and independent Luna xhigh review are complete at clean,
pushed source `2a3af0c2d3a895d5c7ca9fead0dd437065f01d86` on
`codex/vp-0225-subtitle-cadence-20261102`. Full x64 Release qualification passes
1,050 native contracts plus all 18 standard and 18 beta actual-renderer scenarios.
Both matching-binary motion suites pass 220/220. Total: 1,526 passing check
executions, zero failures. Motion reused the exact qualified binaries and verified
the qualification receipt before and after its runs.

The repair recognizes configured-fill Classic poses, advances safe current UNKNOWN
sources from existing raw/candidate occupancy without new caption/native/fit proof,
preserves current raw margins at caption confirmation, and records selected-source
frame/capture identity correctly. No extra detector/text scans or configuration edits.
The existing AR dependency through `788554a1` is explicitly integrated; the other
agent's branch/worktree and deployed player remain preserved. Review the focused delta:
https://github.com/billslack2/videoprocessor/compare/788554a100afe0af2352b904fe1870d80fb75bb7...2a3af0c2d3a895d5c7ca9fead0dd437065f01d86
No beta merge, deployment, or public release has been performed.

Coordination check at completion: VP-0222 now has a newer detector-only candidate
`25900500` under its own qualification; `788554a1` remains deployed. This VP-0225
qualified build does not include that newer candidate. Coordinate the combined
integration/deployment with the AR work and qualify the actual combined source;
do not represent either separate receipt as combined-build qualification.

Remaining acceptance: deploy only on explicit request, then repeatedly replay the
original scene under matched settings, check caption preservation and surrounding
scenes, and collect matched source-gap percentiles plus processing/queue costs on
relevant physical input rates. The recording is desktop output; no exact native
capture exists for sources5671/5672. The new 640px progression fixture observed one
UNKNOWN candidate and no eligible raw/no-candidate rows; its pass is not an exact
replay. Native raw-only cases and broader 4K/runtime coverage are separate evidence.
Keep Review until live acceptance and the coordinated AR integration/merge decision.

Final evidence: [validation](../assets/VP-0225/qualification-20261102/validation.json),
[qualification](../assets/VP-0225/qualification-20261102/runtime-qualification.json),
[motion](../assets/VP-0225/qualification-20261102/motion-renderer-tests.json).
All four final TRX files are beside these receipts; full logs remain under the source
worktree's ignored artifacts directory.

Current source is clean and pushed at `2a3af0c2d3a895d5c7ca9fead0dd437065f01d86`
on `codex/vp-0225-subtitle-cadence-20261102`. The first full runtime run caught
an actual UNKNOWN-to-PRESENT 4px reversal: the confirmed branch discarded the
still-current raw occupied-side margin. That is repaired and has a dedicated
native counterexample. The focused handoff, 4K pan and current-source progression
checks pass on both dependencies. Final strict decoded-DV checks also pass for
Classic and Reconstruction; the oracle joins documented unprocessed UNKNOWN
continuations to original input/admission/native context without inventing a
certificate. Existing per-output glyph coverage, fixed native size, monotonic
pan, shader readiness and complete-image repeat checks remain mandatory.

A diagnostic defect exposed by the stronger joins is corrected: successful
output records used to omit original source-frame/capture IDs. They now carry
the selected snapshot's frozen identity rather than borrowing current-frame
metadata. Final full Release qualification plus matching-binary motion suites
are running; no deployment, merge, live-scene acceptance or Done claim yet.

The incident host identity is resolved to `ad9e7701e05393c1d79fb5f9b11b5a88484c3a35`:
the originally observed runtime sidecar SHA256 `B0C8B8FEBB0DF03C6574BAC19226A483D952F2E4DEFA9F695FEEA6CB769AAD8C`
matches the canonical VP-0222 deployment record, and process36456/instance
A3E5EE18 agree with that investigation. `788554a1` is the subsequent related
repair, not the recording's binary.

Historical provenance was independently checked against GitHub: remote RC4 is
`6d5f8ad4` and remote RC6 is `7af23836`. The initially inspected local RC6 tag at
`e3a34b6c` was stale; that is a release ancestor of remote RC6. The remote RC6
crop policy is identical and its seven renderer-line differences add no canvas
wait, so the post-RC6 regression finding stands. The linked research is corrected.
The 640-pixel new progression fixture observed one UNKNOWN candidate and zero
eligible raw/no-candidate rows; its pass is not claimed as an exact native replay
of sources5671/5672. Native raw-only cases and the broader 4K/runtime matrix are
separate evidence; original-scene live replay remains acceptance.

2026-11-02 repair progress: three Luna xhigh research lanes compared pre-RC6/RC6
behavior, current admission, and exact incident source IDs, then discussed the
patch safety. The whole-source canvas wait first appears in post-RC6 commit
`01c1e13f`; public RC4/RC6 have no such gate. Classic also compares an actual
configured-fill pose against unfilled native X bounds, rejecting the valid
[16,410,3824,2030] pose against [0,270,3840,1890]. Source5669 has a current
unconfirmed candidate extending to y2042; retaining the old pan would clip it.

Existing AR/subtitle dependency `788554a1` was explicitly merged into the fresh
beta-created worktree as `24ae1e2`, preserving the other agent's tree. Focused
fix commit `443b6c3b` maps previous poses against the configured viewport and
uses exact-current raw/candidate bounds to constrain same-size Classic placement.
UNKNOWN stays UNKNOWN and continuation grants no new native/fit/caption proof.
It adds no detector/text scans; guards preserve stale-context rejection,
occupied-side continuity, bottom priority, and genuine picture expansion.

The incident-native regression first failed against unchanged production at the
expected pending-source admission assertion, with successful Release compilation.
After the repair, all 1,049 native contracts passed. Review tightened horizontal
expansion test coordinates and renderer progression assertions afterward; final
full qualification at the exact clean `443b6c3b` source is now running. Host and
both renderer x64 Release builds have passed; final test results remain pending.
The new actual checks join eligible candidate and raw/no-candidate sources to
actual submissions, require consecutive source IDs, and inspect authority flags.
No live movie replay, deployment, beta merge, or public release is claimed.

Evidence: [research](../assets/VP-0225/review-20261102/research.md),
[pre-fix failing regression](../assets/VP-0225/review-20261102/native-before.trx),
[native iteration](../assets/VP-0225/review-20261102/native-after-iteration.json).

2026-11-02: User authorized starting the review and optimization with multiple
Luna extra-high reasoning subagents, including a dedicated pre-RC6 history
review. Work is isolated on `codex/vp-0225-subtitle-cadence-20261102` in
`E:\codex\videoprocessor\vp-0225-subtitle-cadence-20261102`, created from freshly
queried/fetched current remote beta/default `v1.4.02-beta` at
`a591ad3b532771911e85f8ef026958279f2065f0`.

Readiness review: the incident is preserved with source/output IDs and timing;
subtitle canvas admission and the final submission wait are identified code
boundaries. Native policy plus actual renderer progression/pixel checks can
validate the correction. Frame/cue/generation/format ownership and genuine
picture expansion must remain safe. No configuration change is required.
Architecture and regression analysis are the first implementation activities;
no production patch or qualification has yet been claimed.

Related AR repair `788554a100afe0af2352b904fe1870d80fb75bb7` is deployed from the
separate `codex/reconstruction-admission-stall-20261102` branch. It fixes UNKNOWN
inspection starvation for Black/Reconstruction while explicitly leaving Classic
unchanged. The active AR chat is currently idle; its tree and deployment are
preserved. That repair branch contains substantial unmerged work above beta,
so inspect dependencies before importing any changes. This story will validate
shared fixes and target remaining Classic/performance issues rather than make
competing detector changes. Research lanes: pre-RC6 behavior/history, current
admission/submission logic, and incident/reproduction evidence. Agents discuss
findings before the patch design. No deployment is authorized by this start.


2026-11-02: Created at the user's request after reviewing a recording and the
matching deployed log. Scope is code review, optimization, coordination with the active AR work,
and measured regression validation. Implementation has not started; no fix,
source baseline, merge, or deployment is claimed.

