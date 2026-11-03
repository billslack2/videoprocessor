# VP-0225: Review and optimize subtitle processing to eliminate repeatable playback stalls

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

## Problem and objective

The same scene stutters severely on every replay while surrounding scenes play
normally. Review the subtitle analysis, readiness, canvas admission, AR
interaction, and presentation path and remove their adverse impact on picture
cadence. Older remote versions that the user reports did not have this issue
may provide useful design or diagnostic evidence; historical comparison or
bisection is optional, not a required implementation method. Preserve correct subtitle behavior, source-derived picture detection,
and current picture/caption safety rather than trading stutter for lost content.

## Reproduction and incident evidence

- User recording: `C:\Users\bslac\Videos\2026-11-02 18-55-54.mp4`;
  19.983 seconds, 1199 frames, 2560x1440, recorded at 60 fps.
- At recording 8 seconds, the drawn correlation timecode is
  `23:56:02.950Z A3E5EE18 G2 O2400 S5579`. The main logged episode at
  18:56:06.660-18:56:08.375 local is approximately recording 11.7-13.4 seconds.
  Use the drawn timecode for precise alignment, not the filename alone.
- The recording is a desktop capture of VP playback, not the original HDMI
  source pixels. It supports pacing diagnosis but is not an exact native-frame
  detector/cleanup fixture. Capture source frames separately if required.
- Incident configuration: 3840x2160 23.976 fps, DeckLink Quad HDMI Recorder (2),
  v210, SDR REC.709; VP Renderer windowed on approximately 59.95 Hz display;
  Standard Scope, Scope zoom/viewport, Classic subtitles, NLS off, motion off.
  Queue capacity 10, observed depth 3. Runtime title identifies
  `codex/dark-scope-relative-edges-20261102`; recover exact host/renderer source
  identities before comparing builds rather than assuming title equals commit.
- Matching `vp.log` shows captured delivery approximately 23.976 fps with
  `skipped_input_frames=0` and `total_missed=0`. GPU frame times are approximately
  4 ms against a source frame period of 41.708 ms. Those counters do not prove
  smooth presentation.
- Submitted sources advance `5668 -> 5673 -> 5677 -> 5681 -> 5685 -> 5689 ->
  5693 -> 5697`, with repeated gaps around 167 ms (roughly 6 picture updates/s),
  an initial 212.756 ms gap, and an earlier 289.054 ms gap at 18:55:59.
- Missing sources 5670-5672 explicitly log subtitle canvas admission `ready=0`,
  `last_submitted_source=5668`, and reason
  `subtitle inspection pending; keep the last complete movie canvas`.
  Presentation skips include 26 in a 3297 ms window ending 18:56:09, classified
  `unclassified-no-submit`; the recorded renderer-drop counter stays zero.
- This implicates readiness/submission gating. It does not yet prove which
  function, worker scheduling decision, or historical change caused the stall.
  Pacing trace callback gaps are between submitted-source records; do not infer
  physical DeckLink delivery stopped from those gaps when delivery telemetry
  independently shows no misses.

Versioned evidence: [findings](../assets/VP-0225/incident-20261102/findings.txt),
[matching log window](../assets/VP-0225/incident-20261102/vp-log-window.txt),
[configuration](../assets/VP-0225/incident-20261102/VideoProcessor.cfg),
[state](../assets/VP-0225/incident-20261102/VideoProcessor.state), and
[artifact hashes](../assets/VP-0225/incident-20261102/manifest.json).
Configuration/state are investigation-time snapshots; the recording establishes
incident selections. The original recording stays at its user-provided path.

## Review scope and optional historical reference

1. Discover the current beta integration branch and tip on
   `billslack2/videoprocessor`, fetch it, and use a clean source worktree under
   `E:\codex\videoprocessor`. Remote `main` is the story base, not the source
   integration base. Preserve concurrent source work and the deployed setup.
2. Another agent is already investigating related AR behavior where subtitles
   stalled the entire renderer. Treat this as potentially the same underlying
   issue. Before implementation, identify that work and its current findings,
   agree on scope/ownership, and reuse shared reproductions or fixes. Do not
   start competing changes to the same pipeline or treat subtitle and AR stalls
   as proven independent bugs. If that work resolves this incident, validate it
   against this story and record the shared resolution rather than forcing a
   separate optimization. Older remote branches/tags/commits are an optional
   reference if useful; record exact revisions and comparable conditions when
   drawing conclusions from them.
3. Trace capture -> analysis jobs -> conversion/preparation -> subtitle result
   publication -> native/caption canvas admission -> submission/presentation.
   Explain every omitted source in the incident, distinguishing deliberate
   admission suppression, worker backlog, stale/mismatched results, blocking
   waits, missed deadlines, render load, and actual capture loss.
4. Measure full/incremental scans, repeat/duplicate work, queue contention,
   locks, cancellation, copy/upload/readback costs, geometry invalidation, and
   whether subtitle work unnecessarily re-runs for each output or holds the
   picture path while a bounded asynchronous result is pending.
5. Produce a review with concrete code references, any relevant historical evidence,
   measured bottlenecks, proposed optimizations, and correctness implications.
   Keep changes focused; coordinate shared ownership with VP-0207 and VP-0214.

## Optimization requirements

- Resolve the cause of picture stalls while subtitle/AR work is pending. A
  bounded fallback may be appropriate, but choose the approach from code review,
  measured evidence, and coordination with the AR work; no particular pipeline
  redesign is prescribed. Any retained layout or pending result must not cause
  prolonged old-picture holds. Handle transitions and new captions safely.
- Keep expensive work bounded and off the critical presentation path where
  practical. Prefer eliminating redundant work and safe result reuse over
  increasing latency, queue size, timeouts, or weakening detection thresholds.
- Preserve frame/cue/generation/format identity and reject stale work after
  seek, source change, restart, profile switch, and geometry changes. Reusing
  layout is not permission to reuse stale erasure/composition or caption facts.
- Preserve picture and caption pixels, correct Classic/generated-gray/Black/Off
  behavior, backing cleanup boundaries, cue acquisition/release, and subtitle
  placement. Do not simply bypass admission guards, disable subtitles, or crop
  away unverified content to meet cadence.
- Derive active boundaries and aspect from source pixels, independently of the
  2.35:1 screen, CIH, zoom, fitting, fill limits, and NLS. No aspect snapping or
  detector-threshold shortcuts as a performance workaround.
- Make intentional suppressed submissions and their causes observable. Zero
  renderer drops must not conceal the measured playback stall.

## Acceptance and validation

- Reproduce and explain this exact incident, including the repeated one-in-four
  submissions, then replay the original scene repeatedly before and after the
  change under matched conditions. Validate surrounding normal scenes too.
- Record matched before/after source-to-submit traces and available
  actual presentation evidence, frame IDs, gap distributions (median/p95/p99/max),
  suppressed submissions and reasons, CPU/GPU time, worker backlog and latency.
  Clearly distinguish submitted from displayed frames when presentation timing
  is unavailable. Account separately for normal 23.976-to-59.95 display cadence.
- Demonstrate no subtitle-induced multi-frame submission stalls, recurring
  approximately 167 ms cadence, or 200-289 ms holds in the reproduced scene.
  Delayed optional subtitle work must not suppress otherwise eligible source
  submissions. User playback acceptance is required to close the reported bug.
- Establish measured before/after subtitle costs and a bounded scheduling budget
  at 23.976/24, 25/30, and 50/59.94/60 fps on supported representative paths.
  Verify the fix does not merely defer work, add latency, or increase backlog.
- Add regressions exercising real frame progression with delayed, missing,
  incomplete, stale and out-of-order subtitle results; pending canvas admission;
  cue appearance/disappearance; geometry changes; and restart/seek invalidation.
  Include actual pixel checks for picture/caption preservation and relevant
  existing detector, backing cleanup, composition, and temporal tests.
- Test Classic, generated-gray/reconstruction, Black and Off; auto/fixed zoom;
  subtitle/picture-boundary crossings; bright/dark scenes and overlays. Preserve
  supported native YUV and RGB behavior, using focused format coverage.
- Complete an x64 Release build and relevant regression checks. Review code and
  measured evidence before acceptance. Any later authorized deployment must
  use the matching Release host/renderer pair and preserve user configuration.

## Deliverables and next action

Deliver review findings, the focused optimization or linked shared AR fix,
regression tests, before/after timing evidence, and live acceptance result.
Include historical comparisons only if useful to the investigation. First
identify the active AR investigation and align ownership/findings, recover the
incident binary identity, and inspect the current remote beta. Record readiness
and the implementation worktree before moving this story to In Progress.

## Tracker audit at creation

Remote main at creation was `cc6fc8e7`. The ID audit found 242 canonical items
and 242 index rows with no duplicate or missing IDs; VP-0225 is the next root.
Existing tracker inconsistencies were observed and left unchanged: VP-0211 is
in `in-progress` with status In Progress but indexed Review; VP-0088/VP-0168 have
legacy status capitalization (VP-0168 also in its index row); VP-0070 does not
have a parsable standard Status heading. Numerous older records append prose
to their status line. These are unrelated tracker-repair work; this new record
uses exact Backlog folder/status/index agreement.
