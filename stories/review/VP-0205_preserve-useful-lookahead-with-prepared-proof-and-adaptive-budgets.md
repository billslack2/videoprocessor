# VP-0205: Preserve useful lookahead with prepared proof

## Status

Review

Reduced [PR #193](https://github.com/billslack2/videoprocessor/pull/193) merged into
`v1.4.02-beta` as `b1415da4e2cc12807f390f4b722c459be9a17c08` on 2026-10-23.
The merge tree exactly matches the reviewed/tested feature tree. Exact clean source: `acbae401f553ba2561c457ed94c31766335c0822`;
base: `e3dcad1a52c4f1d4bdf29d41ce47d72d6d4cab7a`. Worktree remains
`E:\codex\videoprocessor\pr193-safety-review-20261023`.

The user requested the scope split: keep prepared proof/cache/worker improvements,
and move adaptive timing plus architectural evaluation to
[VP-0208](../backlog/VP-0208_lookahead-scheduling-and-smooth-presentation.md).
The budget class, GPU timer and timing tests exactly match beta. Renderer adaptive
GPU observations/expanded allowance are removed. Beta's pre-existing limited
boundary-skip heuristic is retained and explicitly subject to VP-0208 review.
All other prepared-proof code and the previously reviewed fixes remain unchanged
from the pre-split head; protected recent LUT/crop/subtitle paths match beta.

- Clean serial full x64 Release solution Rebuild passed: 73 warnings, zero errors,
  host and both renderers included. Initial incremental LNK1318 PDB failure is
  preserved; the clean rebuild resolved it.
- **1,588/1,588 selected native regressions passed**, zero failures/skips, across
  39 classes, including late/cold workers, zero-new-pixel proof, exact ownership,
  cancellation retention, existing budget/cadence, black reference/crop policy,
  API29/LUT and GPU subtitle composition. All five named critical regressions
  and every selected class are present in the TRX.
- Eight genuine runtime records match their Release/x64 binaries before/after
  tests; source stayed clean and binaries unchanged.
- [Exact-head validation and preservation evidence](../assets/VP-0205/pr193-prepared-split-20261023/validation.json).
- No deployment, playback restart or active configuration edit. PR #192 is still
  separate and needs rebase/combined validation after this integration.

Retain Review after merge: live prepared-proof adoption and sustained worker/GPU/
queue/memory/cadence qualification remain open. The adaptive policy criterion is
transferred to VP-0208, not marked satisfied. The user also asked to evaluate
whether the current policy is sound, whether more work should be async, and
alternative scheduling architectures; VP-0208 records those options and a bounded
comparison spike with no analysis-caused jitter or source dropping as its goal.

### Pre-split review evidence

Current PR head is `13f43bcb6b99581ffcdc1e800ece471565477a80`, rebased onto
current/default `v1.4.02-beta` at `e3dcad1a52c4f1d4bdf29d41ce47d72d6d4cab7a`
on 2026-10-23. [PR #193](https://github.com/billslack2/videoprocessor/pull/193)
remains draft and unmerged. The clean review/build worktree is
`E:\codex\videoprocessor\pr193-safety-review-20261023`.

- Preserves merged #191, #194, #195 and #196. The two rebase conflicts were
  picture evidence and test registration: retain unresolved Full-range matte
  rejection/shared retention reference, add budgeted eligible recovery, and
  retain both test sets. Added cross-format measurement/retention parity coverage.
- Full x64 Release solution build passed (73 warnings, zero errors), including
  host and both renderer variants. Eight genuine runtime records match the
  produced binaries before and after testing.
- All **2,525/2,525 selected native regressions passed**, zero failures/skips,
  covering prepared proof, cancellation, ownership, adaptive admission, current
  black-reference/crop policy, API29/LUT and subtitle/GPU composition behavior.
- No new deployment, playback restart or active configuration change. Historical
  performance/deployment measurements below predate this rebase. PR #192 remains
  separate and unmerged; the full combination still needs conflict resolution
  and validation.

### Timing requirement and remaining merge concern

The user explicitly accepts some additional steady latency, but does not accept
jitter or dropped frames caused by optional analysis. Qualification must therefore
measure presentation interval variation and source continuity, not reject a stable
buffered delay merely because latency increases.

The original 2 ms qualified ceiling first appears in stabilization commit
`12b2dfaf` on 2026-10-15. It bounds optional synchronous render-thread work,
not the whole lookahead window or background jobs. No measured optimum for the
numeric 2 ms choice was found in that history. The adaptive policy is unchanged
by this rebase.

A deterministic probe of the actual budget class supplies a specific concern:
with about 5 ms before the next refresh, latest CPU/GPU samples of 1/2 ms, and
an older 8 ms GPU peak retained in history, it skips that boundary and grants
10.416 ms of optional foreground work. The older peak is useful as a conservative
reserve, but does not establish that the next frame must miss the earlier boundary.
A grant is a maximum; the probe does not show actual grant utilization or a live hitch.

Code tracing shows the skipped-boundary flag only extends the analysis ticket;
it does not shift output PTS or establish a larger steady queue target. Persistent
stall/backlog can trigger existing source-dropping recovery. Thus this mechanism
has not established the user's acceptable steady-latency tradeoff. Before merging
the adaptive part, review the boundary rule against a stable presentation schedule
and qualify actual interval variance, dropped/unintended repeated frames, useful
proof adoption and sustained worker/GPU/queue-memory pressure. Deliberate bounded
buffering and earlier prepared work are the preferred way to trade latency for
smooth delivery. Native tests validate implemented policy and pixel contracts;
they do not establish these live performance properties.

[Current rebase validation, preservation checks, probe and raw test/build evidence](../assets/VP-0205/pr193-rebase-20261023/validation.json)

## Historical pre-rebase implementation and deployment
Implementation is ready for code/build/test review in separate draft
[PR #193](https://github.com/billslack2/videoprocessor/pull/193). Standalone PR runtime source is
`be23c69ffb41bf54d3e112f37692a607d593930b` (prepared-proof implementation
`19767f74d5a6e7cfb2509abcf2a051227502c9a6`, followed by the reviewed guard
avoiding unused full-raster retention priming). The discovered integration base at that time
was `v1.4.02-beta` at `f7c7a8204f7a2cace68b3b40774406680af6c0d8`.

- Published PR head: `a23c211877685fbe31223c9d9c246ccf3bea008f`; the last
  two commits change documentation only after the tested runtime `be23c69f`.
- Branch: `codex/vp-0205-lookahead-workahead` in
  `E:\codex\videoprocessor\vp-0205-lookahead-workahead`.
- Clean serial x64 Release solution Rebuild at `19767f74` passed; the final
  full solution Release Build at `be23c69f` also passed, including both renderer
  variants. Native test DLL and analysis-library hashes are identical across
  those builds. Exact source/binary hashes are preserved with validation evidence.
- All 1,801 selected native regressions passed, with zero errors/skips/failures,
  including exact-key cache, cancellation, cold whole-window dispatch,
  raw-completion-after-prime, zero-foreground delivery, cadence/phase/GPU issue-age,
  caption/refinement, first-source and D3D subtitle-composition cases.
- Independent threading-architect and principal-engineer reviews found no
  remaining correctness/lock-order blocker. Their final guards preserve an
  existing certificate across a missing consecutive sample, skip unused caption
  waiting when independent subtitle proof is accepted, reject stale GPU queries
  using actual issue age, and avoid no-consumer full-raster component priming.
- Proposed decision: retain draft for live capture/GPU-pressure qualification;
  do not merge/release based on synthetic CPU delivery timings alone. The user
  subsequently authorized local deployment; see the integration and receipt below.
- Deployed on 2026-10-23 from clean x64 Release integration `24e0dd10`, retaining
  all previously installed fixes. Configuration was unchanged; playback was not
  started. PR193 and PR192 remain separate and unmerged.

## Objective

Preserve lookahead as a core picture-quality feature: valid crop and caption
proof should be available for its first eligible displayed source frame. Retain
configured lookahead depth while reducing avoidable repeated scans, discarded
completed work, and deadline cancellations. Keep current beta presentation admission unchanged for this integration.
VP-0208 evaluates its adequacy and alternatives; 2 ms is not treated as an optimum.

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

## Implemented design

1. Keep configured lookahead depth. Raw preparation additionally covers the
   three distinct future sources required by outward proof. Four independent
   bounded workers keep raw/subtitle/proof work from waiting on each other;
   nearer queued sources may replace only farther pending work. New raw/component/
   outward admission uses try-locks and never waits for job completion. Existing
   subtitle submission retains its short queue mutex; no worker join is added
   to frame publication.
2. Retain immutable raw/global/darkness measurements and complete base-relative
   retention/contrast/caption components. Each source cache has two entries per
   component kind; exact frame/storage/ownership/domain/generation/base and
   caption nomination keys isolate replacement contexts and stale completions.
   Complete negatives are reusable; pending/cancelled work is not evidence.
3. Submit an entire three-source outward request even when dependencies are
   cold. It reuses exact ready raw/retention results or computes/publishes them
   itself without another render pass. Concurrent cold fallback can duplicate
   bounded work; explicit reuse/fallback counters and measurements expose it.
4. Consume completed components before new scans. Later cancellation retains
   unrelated complete work. Zero foreground allowance can still consume cached
   proof; a miss never enters a foreground evaluator. Pending eligible caption
   evidence stops the publishable prefix without casting a provisional final
   vote. Only the first three consecutive complete samples replace expansion
   authority. Existing live native-pixel/owner/model checks remain authoritative.
5. Retain exact current-beta presentation admission and timer behavior. The
   previously implemented adaptive CPU/GPU tails, uncapped qualified allowance
   and expanded boundary rule are deferred to VP-0208, recoverable at `13f43bcb`.
6. Fixed aggregate telemetry separates selected/ready/required depth, stage
   cache/work/cancellation, complete work retained after cancellation, async
   ready/late/invalid/no-change/stale, raw/retention fallback/reuse, publication,
   actual adoption/rejection and source lead. Reporting uses the existing
   two-second path. Retained counts represent events, not unique source frames.

## Acceptance criteria

- [x] Existing configured lookahead depth remains intact. Reduced ready-prefix
      depth is explicit in telemetry and cannot masquerade as full-horizon proof.
- [x] Valid prepared evidence supports earliest-eligible-frame outward/inward
      transitions, full-raster return, caption protection and subtitle continuity.
- [x] Render publication never waits for workers. Queue/memory and obsolete-job
      behavior remain bounded, including cancellation, restart and shutdown.
- [x] Reuse requires immutable pixel ownership, exact source/storage interpretation,
      frame identity, analysis domain and transport/format/viewport/policy/continuity
      generations. Base-relative certificates additionally match bounds, relevant
      owner/subtitle context and history revision.
- [x] Partial scans cannot become black, clearance or crop authority. Current-pixel,
      owner and live-model validation remain authoritative; stable-reference safety
      is not weakened to improve adoption counts.
- [x] A later helper cancellation retains unrelated complete measurements safely;
      overlapping windows reuse completed proof while its full dependency key remains
      current and do not cast duplicate temporal votes.
- [x] Reduced PR preserves beta admission and passes exact-head Release/native
      validation. Adaptive policy acceptance is explicitly transferred to VP-0208.
- [ ] Matched measurements demonstrate useful ready/adopted proof and transition
      timing alongside CPU/GPU/presentation tails, deadline misses, worker cost,
      queue depth and memory. Completion percentage alone is insufficient.

## Validation and remaining qualification

The native/Release implementation contracts above passed. Matched live replay,
actual displayed-source adoption and sustained GPU-pressure/queue/memory tails
remain outstanding; live prepared-proof acceptance remains open. Adaptive architecture acceptance
is explicitly transferred to VP-0208; it is not declared complete by this split.


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
separate open PR at `43b81e9063e0531f7ac2b1f589238d6ad6d37d33`. Its
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
- [Source design](https://github.com/billslack2/videoprocessor/blob/codex/vp-0205-lookahead-workahead/docs/VP-0205_LOOKAHEAD_WORK.md)
  records the implemented design and final validation. Full Release build logs,
  hashes, native result summary and controlled three-source proof-delivery
  evidence are linked below. Initial local MSBuild reference-metadata and lambda
  capture errors were corrected; an incremental v142 PDB link failure was resolved
  by the successful clean serial Rebuild. Nonfatal conversion/shadow warnings remain.
- ID audit before allocation: 223 canonical items and 223 index rows, maximum
  root VP-0204, next unused VP-0205; no duplicate/orphan ID or registry conflict.
  Existing historical status paragraphs were compared by normalized state prefix.
  VP-0070 has no standard `## Status` heading, while its folder/index say Blocked;
  VP-0168 retains historical lowercase state spelling. Decision: record these
  pre-existing formatting anomalies and leave unrelated stories unchanged.

## Implementation measurements (2026-10-23)

The final clean `be23c69f` controlled harness passed 15 three-distinct-owned4K
fixtures and full retention/certificate parity. Six expansion fixtures delivered
606/606 valid proofs on each direct 2/3/4 ms path and ready-only zero-new-pixel
path, with effective first source 100 and final observation 102. Ready-only
foreground median range is 0.0039–0.0043 ms (p95 0.0158–0.0197 ms), versus direct
2 ms-path median 0.6034–0.8653 ms (p95 0.7874–1.1064 ms).

Across all outcomes direct completion is 1382/1515 at2ms, 1512/1515 at3ms and
1515/1515 at4ms; ready-only is1515/1515. The shortfall is confined to negative /
no-change fixtures, so this run does not demonstrate increased useful-proof
completion from a larger cap. Historical helper sweeps above have different
fixtures/methods and must not be treated as a matched production comparison.

A cold complete outward worker computes3raw +3retention inputs: valid-proof
median service1.1404–1.1977ms; warm reuse0.0086–0.0097ms with zero new scans.
All-fixture cold medians1.1404–4.272ms, maximum5.173ms. Worker CPU cost is moved /
reused, not eliminated. Observed-ready latency is dominated by roughly15–16ms
Windows timer scheduling despite requested Sleep(1); it cannot estimate production
dispatch latency. P010/P210 use synthetic planar adapters. This is CPU proof
preparation/delivery, not live displayed-source adoption, GPU qualification or
full renderer replay.

- [Relocatable harness, raw CSV and complete provenance](../assets/VP-0205/proof-delivery/README.txt).
- [Controlled result summary](../assets/VP-0205/proof-delivery/proof-delivery-results.json).
- [Final Release source/binary hashes](../assets/VP-0205/validation/release-validation-final.json).
- [Native test result summary](../assets/VP-0205/validation/native-validation.json).

## User-authorized local deployment (2026-10-23)

The user requested `deploy it`. The existing installation was clean source
`e1996d67e42df7ee0ba63e2fc7577a43d132af48`, confirmed by prior deployment
receipt, actual host/renderer hashes and startup identity. Standalone PR193 would
remove installed asynchronous memory-backed logging, fullscreen activation and
NVIDIA worker fixes. An isolated deployment integration preserves those features.

- Deployment source: [`24e0dd10430e5140c06c1fa1bd106c9bbde61ef7`](https://github.com/billslack2/videoprocessor/commit/24e0dd10430e5140c06c1fa1bd106c9bbde61ef7).
- Published recovery branch: `codex/vp-0205-deploy-20261023`;
  worktree `E:\codex\videoprocessor\vp-0205-deploy-20261023`.
- Started from freshly fetched current/default beta `f7c7a820`; first local merge
  `4d199e23` retains the installed e199 tree exactly, then integrates PR193 `a23c2118`.
  This deployment branch leaves both GitHub PRs separate, draft and unmerged.
- Threading and principal reviews resolved renderer/caption integration, retained
  exact installed GUI/logger/NVIDIA/cache sources and exact new proof helpers,
  and verified union project registrations without missing/duplicate entries.
- Clean full x64 Release solution Rebuild passed; **348/348 focused integration
  regressions passed**, covering retained logging/focus/NVIDIA/cache behavior and
  new proof, cancellation, caption, phase/GPU-age/admission/worker contracts.
  All three x64 binaries contain the full UTF-16 source commit identity; genuine
  Release runtime records match their binary hashes. All18 dependency DLLs already
  match the installed files and require no replacement.
- Deployed matching `VideoProcessor.exe`, standard renderer, beta renderer and
  their three genuine runtime records to `C:\Videoprocessor\vp`. All6 replaced
  files were backed up and hash-verified before writing. The transaction held the
  updater's gate and exclusive handles; copied hashes and independent subsequent
  verification match the build. Rollback support was prepared but not needed.
- Backup: `C:\Videoprocessor\vp\backup-vp0205-24e0dd10-20261023-114702-242`.
- Configuration edits: **none**. Existing configuration/state/manifest hashes were
  unchanged. No source-example settings, dependencies, Config binaries, shader
  cache or LUTs were copied. Playback was stopped and remains stopped; no new
  startup/capture or live adoption/GPU-pressure result is claimed.

[Deployment receipt](../assets/VP-0205/deployment-24e0dd10/deployment-receipt.json),
[build and binary identity](../assets/VP-0205/deployment-24e0dd10/validated-build.json),
[independent integration audit](../assets/VP-0205/deployment-24e0dd10/integration-review.json),
and [byte-hashed build/test/transaction evidence](../assets/VP-0205/deployment-24e0dd10/evidence-manifest.json)
are preserved. Review/live qualification criteria remain open; deployment alone
is not acceptance, a merge, or a public release.
