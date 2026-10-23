# VP-0205: Preserve useful lookahead with prepared proof and adaptive budgets

## Status

In Progress

Implementation authorized by the user's `proceed` on 2026-10-23. Work continues
on the existing separate branch and draft PR #193, freshly verified against
current/default beta `v1.4.02-beta` at `f7c7a8204f7a2cace68b3b40774406680af6c0d8`.
PR #192 remains unmerged. Only necessary picture-analysis prerequisite APIs may
be reused explicitly; its logging, NVIDIA and fullscreen changes remain outside
this story. Independent threading, proof-correctness and telemetry work is active.

- Source branch: `codex/vp-0205-lookahead-workahead`.
- Clean source worktree: `E:\codex\videoprocessor\vp-0205-lookahead-workahead`.
- GitHub default and latest beta: `v1.4.02-beta`, discovered and fetched at
  `f7c7a8204f7a2cace68b3b40774406680af6c0d8`.
- Separate draft [PR #193](https://github.com/billslack2/videoprocessor/pull/193),
  starts implementation from planning commit `3946bb3a2bedf9552f2fd346013a11b0c4743f4c`.
- Next action: implement bounded prepared-proof reuse, independent completed-work
  retention, adaptive presentation admission and useful-result telemetry; validate
  with native tests, controlled measurements and an x64 Release build.

## Objective

Preserve lookahead as a core picture-quality feature: valid crop and caption
proof should be available for its first eligible displayed source frame. Retain
configured lookahead depth while reducing avoidable repeated scans, discarded
completed work, and deadline cancellations. Protect presentation timing with
measured admission rather than treating the existing 2 ms ceiling as an optimum.

## Evidence

The existing 1 ms unknown-phase allowance and 2 ms qualified-phase ceiling predate
PR #192. That PR adds cooperative cancellation inside expensive helpers. Late
cancellation can discard earlier completed observations in the same invocation;
consuming the selected source loses its first-frame opportunity. Independent raw
measurements, subtitle work, outward-worker results, and temporal interpolation
continue outside that synchronous ticket.

The preserved 2026-10-22 capture has 16,165 observed 23.976-to-23.976 Hz attempts,
13,745 completed calls and 2,432 optional deadline overruns. Twelve windows contain
576 attempts with zero completions, about 1.202 s of accumulated optional work,
and zero reported source/display-period exceedances. At 59.94-to-59.94 Hz,
50,249 of 50,250 calls completed, with three optional overruns. These are retained
log observations, not a complete quality/adoption rate or evidence of dropped
presentation frames. No 24-to-60 Hz performance windows were captured.

At 24 Hz, sampled post-reserve headroom has median 2.368 ms and p95 4.246 ms.
The 2 ms ceiling binds in 286/376 qualified budget reports. Those rate-limited
samples do not mean the entire 41.7 ms source period is available for analysis.

A separate x64 Release retention-helper sweep completed 1356/1515 trials at 2 ms,
1512/1515 at 3 ms and 1515/1515 at 4 ms. It uses 15 synthetic owned 4K fixtures,
101 rotated trials per allowance, three evaluations of the same fixture and
precomputed raw/global/darkness measurements. It establishes a completion cliff
for that helper, not a safe 4 ms budget for full playback.

Existing expansion tests demonstrate first-frame adoption with sufficient
lookahead versus three source intervals later in one conservative fallback
fixture, approximately 125 ms at 24 Hz. A completed cached outward proof can also
validate at zero synchronous allowance. Completed previews can still have short
ready prefixes or be rejected by stable-reference checks; neither a completion
count nor worker completion alone establishes useful adoption.

## Readiness review

Current beta already has bounded raw/subtitle/outward workers, immutable source
ownership and exact-source timeline admission. Display timing is CPU-wall/phase
evidence rather than a GPU completion guarantee; mandatory-cost tails and new
source/repeat separation must therefore remain conservative. The helper budget
and exact-worker-import prerequisites in PR #192 are known and can be included
selectively without its unrelated runtime changes. Worker code will remain pure
CPU measurement; timeline/authority mutation stays on the render thread. Native
regressions and matched synthetic/replay measurements can prove identity,
publication, cancellation and scheduling contracts. Live GPU/capture coverage
will be reported separately. No configuration migration or UI redesign is needed.

## Proposed implementation

1. Add low-overhead aggregate telemetry for selected/ready/required proof depth,
   stage cost and cancellation, expected no-change/invalid proofs, deadline
   lateness, discarded/repeated work, and valid proof ready before the first
   eligible frame. Record publication, adoption/rejection reason and effective
   source lead. Correlate with source/display cadence and presentation headroom.
2. Share immutable raw/global/darkness measurements across workers and windows.
   The outward worker currently repeats work also performed by the raw worker.
   Prepare expensive base-relative proof ahead of consumption with bounded queues,
   nearest-source priority, deduplication and obsolete-work rejection. Do not
   starve raw/subtitle measurement work behind expensive proof jobs.
3. Retain independently complete measurements when later helpers cancel. Publish
   authority only after each complete contiguous proof succeeds. Ensure idempotent
   observation publication, no duplicate temporal votes and later caption refinement.
4. Replace the universal ceiling with admission based on observed display phase,
   mandatory-work tail cost and a conservative reserve. Distinguish new-source
   from repeated-output costs. Define unknown-phase, cadence-conversion and
   overload behavior explicitly; do not spend a future display interval merely
   to finish analysis. Choose numerical bounds from matched replay/live evidence.

## Acceptance criteria

- [ ] Existing configured lookahead depth remains intact. Reduced ready-prefix
      depth is explicit in telemetry and cannot masquerade as full-horizon proof.
- [ ] Valid prepared evidence supports earliest-eligible-frame outward/inward
      transitions, full-raster return, caption protection and subtitle continuity.
- [ ] Render publication never waits for workers. Queue/memory and obsolete-job
      behavior remain bounded, including cancellation, restart and shutdown.
- [ ] Reuse requires immutable pixel ownership, exact source/storage interpretation,
      frame identity, analysis domain and transport/format/viewport/policy/continuity
      generations. Base-relative certificates additionally match bounds, relevant
      owner/subtitle context and history revision.
- [ ] Partial scans cannot become black, clearance or crop authority. Current-pixel,
      owner and live-model validation remain authoritative; stable-reference safety
      is not weakened to improve adoption counts.
- [ ] A later helper cancellation retains unrelated complete measurements safely;
      overlapping windows reuse completed proof while its full dependency key remains
      current and do not cast duplicate temporal votes.
- [ ] An adaptive allowance respects actual presentation deadlines and conservative
      mandatory-cost reserves for both new and repeated outputs, with explicit
      fallback when timing evidence is unavailable or overloaded.
- [ ] Matched measurements demonstrate useful ready/adopted proof and transition
      timing alongside CPU/GPU/presentation tails, deadline misses, worker cost,
      queue depth and memory. Completion percentage alone is insufficient.

## Validation

Use deterministic native tests and matched source-sequence replay for first-frame
crop/caption decisions, cached proof at zero allowance, late-helper cancellation,
overlapping-window reuse, duplicate-vote prevention and identity/context
invalidation. Cover dark/dense scenes, caption boundaries, full-raster returns,
P010/P210/BGRA, restart/resize/policy changes and sustained overload. Exercise
23.976/24/25/30/50/59.94/60 Hz and source/display conversion including 24-to-60 Hz.
Record controlled comparisons on clean x64 Release builds, plus live presentation
validation under GPU pressure. Separate already-missed output opportunities from
analysis-caused misses and distinguish new-source from repeated-output tails.

## Dependencies and scope

VP-0204 / [PR #192](https://github.com/billslack2/videoprocessor/pull/192) is a
separate unmerged draft at `6e7ce51103269a5fd27ac5adcaa40c3b12a3635a`. Its
cooperative helper budget, exact-worker imports and telemetry contracts are
explicit analysis prerequisites. Include only the necessary analysis support in
this implementation and reconcile with its final APIs when beta accepts it. The
branch starts at current beta and does not carry PR #192's logging, NVIDIA or
fullscreen changes; no blanket dependency merge is part of this work.

Related: VP-0124 safe outward lookahead, VP-0082 buffered active-picture lookahead,
and VP-0070 subtitle safety. This story excludes LUT, focus, logging transport,
NVIDIA, queue-depth expansion, settings deployment and release packaging changes.

## Evidence and tracker audit

- [Captured playback aggregates](../assets/VP-0205/capture-summary.json).
- [Helper budget results](../assets/VP-0205/helper-budget-summary.json),
  [raw trials](../assets/VP-0205/helper-budget-sweep.csv),
  [recorded fixture source](../assets/VP-0205/lookahead_budget_sweep.cpp) and
  [source provenance and limitations](../assets/VP-0205/measurement-provenance.json).
- [Source design](https://github.com/billslack2/videoprocessor/blob/3946bb3a2bedf9552f2fd346013a11b0c4743f4c/docs/VP-0205_LOOKAHEAD_WORK.md)
  in separate draft PR #193. Independent principal review found no material
  factual/scope issue; reuse criteria explicitly allow recomputation after a
  dependency-key change. Raw-trial totals, capture counts, evidence links and
  whitespace checks passed. The recorded helper run is historical measurement,
  not a new production build or a runtime change in this PR.
- ID audit before allocation: 223 canonical items and 223 index rows, maximum
  root VP-0204, next unused VP-0205; no duplicate/orphan ID or registry conflict.
  Existing historical status paragraphs were compared by normalized state prefix.
  VP-0070 has no standard `## Status` heading, while its folder/index say Blocked;
  VP-0168 retains historical lowercase state spelling. Decision: record these
  pre-existing formatting anomalies and leave unrelated stories unchanged.
