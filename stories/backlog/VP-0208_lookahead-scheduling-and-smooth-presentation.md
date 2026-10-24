# VP-0208: Reassess lookahead scheduling for smooth presentation

## Status

Backlog

Created on 2026-10-23 at the user's request while splitting PR #193. This is
initial architecture discovery, not production implementation. First produce a
bounded comparison and a replay/profiling spike; choose an architecture before
starting implementation children. The existing policy is a subject of review,
not an accepted design constraint. Some steady additional latency is acceptable;
optional analysis must not introduce presentation jitter or dropped frames.

## Problem and objective

Make useful crop/caption proof ready before its first eligible source is displayed,
without making the presentation clock wait for optional analysis. Evaluate both
whether adaptive admission makes sense and whether the current policy makes
sense at all. More async work may help, but it still competes for CPU, memory
bandwidth and GPU resources and must have explicit capacity and overload behavior.

PR #193 / VP-0205 merged its prepared-only scope into beta as `b1415da4` on
2026-10-23, after a clean Release build and 1,588 native tests. It retains the
prepared-result caches, bounded worker preparation,
exact ownership/context checks, complete-work retention after cancellation and
usefulness telemetry. Its reduced version restores the current beta admission
and GPU-timer code. This story owns the deferred adaptive policy and the larger
scheduling decision; it does not presume that changing 2 ms to another constant
is the solution.

## Baseline facts and unresolved questions

- Reviewed on 2026-10-24: GitHub default/latest beta remains `v1.4.02-beta`
  at `b1415da4e2cc12807f390f4b722c459be9a17c08`, including reduced PR #193.
  The former `e3dcad1a` baseline predates that merge. Current beta and reduced
  #193 are now one experiment arm, not two independent implementations.
- PR #197 is OPEN DRAFT at `13dc7bb0cc3c195035ed0f41fc81e0f749460685` and
  contains this beta plus newer current-subtitle proof/recovery work. Review it
  as an explicitly pinned candidate, not merged or accepted behavior. PR #192
  remains OPEN at `43b81e9063e0531f7ac2b1f589238d6ad6d37d33`. Its
  `AnalysisEvaluationBudget.h` and `ActivePictureEvidence.cpp` already match
  beta byte-for-byte; its additional cache, logger, NVIDIA-worker and fullscreen
  work still needs reconciliation. Rediscover all three heads before a spike.
- Current policy grants at most 2 ms with qualified phase, 1 ms otherwise, and
  zero new scans for repeats/backlog. It reserves measured mandatory CPU cost and
  already has a limited boundary-skip heuristic. Restoring it does not prove it
  prevents jitter. Audit its phase confidence, reserve, cancellation overrun,
  repeated-output accounting and existing boundary skip as well as the new policy.
- The 2 ms value first appears in `12b2dfaf` (2026-10-15), as a conservative limit
  on optional synchronous work. No measured optimum was found. It is not the
  background-worker allowance, per-future-frame allowance or full lookahead horizon.
- A 24 fps input period is about 41.7 ms; on a 60 Hz display the output cadence is
  about 16.7 ms. Remaining time to a scheduled output, not input period alone,
  determines whether foreground work can fit.
- The deferred implementation uses two-second CPU/GPU tails, separates repeats,
  removes the qualified 2 ms ceiling, and may select a later boundary. Its GPU
  queries measure selected stages, not complete Present/scanout cost.
- The preserved deterministic probe gives about 5 ms to the next boundary with
  latest CPU/GPU samples of 1/2 ms and an older 8 ms GPU peak. It grants 10.416 ms
  after selecting the following boundary. A conservative old peak is not proof
  the nearer boundary is impossible. The allowance is a maximum, not evidence
  of a live hitch or actual time spent.
- Code tracing found no corresponding output-PTS shift or steady queue-depth
  increase. Extending an analysis ticket alone does not establish steady added
  latency. Persistent stalls can reach existing backlog/drop recovery.
- Existing prepared proof is promising: historical synthetic fixtures validate
  identical decisions with zero new-pixel allowance and sharply lower foreground
  cost. They do not prove live adoption or jitter behavior under contention.

## Impact review of recent changes — 2026-10-24

The architecture question remains open. Recent work changes the current-proof
cost and failure model; it does not justify restoring the deferred adaptive
policy or raising the 2 ms allowance. This review compares pinned source and
existing tracker evidence only; it does not run a new performance experiment,
change production code/settings, or deploy anything. The earlier subtitle and
telemetry messages redirected out of this chat are not evidence for this review.

| Recent change | Consequence for VP-0208 |
| --- | --- |
| #193 prepared proof is already in beta | Start with ready-result reuse enabled. Attribute remaining foreground work and missed adoption before adding workers or buffering. Use the historical pre-merge source only for a separately labelled ablation. |
| #197 adds one current-subtitle read budget and orders complete inspection before weaker pending work | Profile mandatory current proof separately from optional future lookahead. The current candidate has 524,288 shared reads, 393,216 inspection reads and a 131,072 backing-phase ceiling; these are nested limits, not additive allowances or milliseconds. `PresentationAnalysisBudget.h` remains identical to beta: qualified 2 ms / fallback 1 ms, with its existing boundary rule. |
| #197 reuses exact detector samples and sufficiently complete worker evidence after fresh cue-plane proof; exhausted work stops sooner | Measure the optimized path before increasing quotas. Initial-grid reuse reduces duplicate reads but previously did not demonstrate CPU acceleration. Recorded complete-worker cases reduce inspection reads from roughly 332–336k to 60,064; this is fixture-specific, not a universal cost bound. Unavailable/narrow worker results still require current inspection and complete configured coverage. |
| #197 distinguishes complete, incomplete and contradictory evidence | Preserve private candidate/outline state through bounded incomplete work, without granting output or stale erasure authority or renewing its lease. Keep current cue proof ahead of optional backing work. A late worker cannot be treated as a blank cue, and incomplete evidence cannot be treated as a successful proof. |
| #197 binds presentation metadata to a newly captured GPU packet and repairs native-stack pressure | Keep pixels, subtitle metadata and source identity atomic across duplicate captures, output repeats and recovery recapture. Bound stack, scratch allocation, ownership and working set as well as worker count. Passing ordinary unit tests did not catch the earlier production-stack overflow. |

The async option must redesign ownership deliberately: `AnalysisLumaReadBudget`
is borrowed synchronous state, and `PreparedCurrentSubtitleCue` is a synchronous
input-binding token, not a queueable job. A worker version needs owned immutable
source storage, a job-owned budget/cancellation lifetime, and publication keyed
to the exact source/policy/continuity. Do not put the existing borrowed budget
pointer or mutable presentation guard on a worker. Current-pixel contradictions,
full configured companion search and finite cleanup proof stay mandatory.

A fixed-buffer design must also define its clocks. Current pending leases count
nominal duration across accepted-source sequence gaps; exact source repeats do
not spend or renew that lease. Complete fresh cue proof and incomplete-evidence
leases have different rules. Added wall latency, source-time expiry, job retirement
and display-output repeats must not be conflated or loosened to hide misses.

The separately recorded [morning recovery review](../assets/VP-0207/morning-13dc7bb0-20261024-0745/morning-event-review.txt)
adds a concrete recovery case: a 209 ms render stall/four-source queue drop precedes
pending expiry, subtitle loss and reacquisition with a nominal 166.8 ms source-time
gap. This does not establish an analysis-caused stall, an actual displayed-pixel
duration, or that a larger lookahead budget would help. Reproduce the transition
with injected stalls, changed display timing, source gaps and lease expiry; keep
ordinary steady-state cadence and recovery qualification separate. Do not disable
emergency recovery or authorize stale cleanup to make the symptom disappear.

Recommended order: preserve and integrate the accepted current-cue contracts,
measure useful worker delivery and foreground recovery on that exact source,
then compare ready-only optional analysis with a deliberate fixed buffering
increment. Evaluate fixed-schedule adaptive admission only if useful measured
slack remains. VP-0207 owns subtitle correctness and its outstanding live replay;
VP-0208 owns scheduling, stable latency and cadence. Discovery can proceed while
#197 is open, with its candidate status explicit. #192's non-pixel stalls remain
a separate confounder to account for before attributing improvement to analysis.

Source anchors: [admission policy at beta](https://github.com/billslack2/videoprocessor/blob/b1415da4e2cc12807f390f4b722c459be9a17c08/src/VideoProcessor-Lib/vprenderer/PresentationAnalysisBudget.h),
[current-cue coordinator](https://github.com/billslack2/videoprocessor/blob/13dc7bb0cc3c195035ed0f41fc81e0f749460685/src/VideoProcessor-Lib/SubtitleCurrentCue.h),
[current inspector](https://github.com/billslack2/videoprocessor/blob/13dc7bb0cc3c195035ed0f41fc81e0f749460685/src/VideoProcessor-Lib/SubtitleCueContinuation.h),
and [immutable packet capture](https://github.com/billslack2/videoprocessor/blob/13dc7bb0cc3c195035ed0f41fc81e0f749460685/src/VideoProcessor-Lib/vprenderer/TemporalRenderer.h).
Existing [candidate qualification](../assets/VP-0207/deployment-13dc7bb0-20261024/summary.txt)
reports 1,964 selected tests and production-stack checks; moving playback,
physical mismatched refresh and HDR visual acceptance remain open. Sample quotas,
quiet sampled logs and synthetic GPU output do not establish a hard deadline.

## Architecture options to compare

| Option | Work arrangement | Questions and tradeoffs |
| --- | --- | --- |
| Current bounded foreground policy | Async preparation plus capped render-thread fallback | Establish what still scans synchronously, how often useful proof is lost, and whether even the existing skip/reserve behavior can perturb cadence. This is the measured baseline, not an assumed correct design. |
| Primarily async analysis | Prepare immutable per-source raw/components/proof on bounded workers; presentation consumes only ready exact results | Can all optional pixel scans move off the render path? Keep only essential current-source validation and bounded metadata admission there. Measure worker deadlines, priorities, contention, ownership and safe misses. Compare coordinated/shared dependency scheduling with separate workers and duplicate cold fallback. |
| Deliberate stable lookahead buffering | Select a bounded presentation delay/queue target so proof starts earlier relative to scheduled display | Derive depth from source cadence and measured preparation tails. Preserve a stable output timeline and source continuity; define startup/re-prime, resize/rate-change, AV-sync/audio ownership and capacity. Do not increase/decrease delay opportunistically every frame or implement delay by a late render call. |
| Adaptive work inside a fixed output schedule | Admit optional work only from verified slack before the already scheduled deadline | Include full mandatory work, uncertainty and real query age; never move a present deadline to fund optional analysis. Determine whether any useful benefit remains after async reuse. May be useful in worker admission rather than on the presentation thread. |
| Combined staged pipeline | Bounded async dependency graph plus deliberate buffering, with a small fixed-deadline fallback only if measured necessary | Compare complexity, observability and operating margins against simpler options. A GPU/compute measurement stage is a separate spike only if profiling supports it; avoid readback/driver waits on presentation. |

No option may count skipping source frames or unintended output repeats as a
successful latency tradeoff. Expected cadence repeats in 24-to-60 output are
normal and must be distinguished from missed scheduled outputs. If preparation
cannot keep up, cancel obsolete optional jobs and use the established safe
picture/caption fallback for that source; do not wait or misapply stale proof.
Do not remove existing emergency queue recovery without understanding its
non-analysis causes; measure it and ensure analysis does not trigger it.

## Bounded discovery and proposed first experiment

1. Map the actual capture, ownership, conversion, raw/component/caption proof,
   render, Present and retirement dependencies. Inventory every synchronous scan,
   wait/lock, GPU query/readback, duplicate job and buffer lifetime. Distinguish
   mandatory safety validation from optional advance proof; validate those labels.
2. Instrument a matched replay with source identity, arrival, scheduled output,
   proof-ready/adopted time, render start/end and actual presentation observations.
   Separate analysis-caused delay from an output already late on entry. Trace
   total CPU/memory use, runnable-worker pressure, queue/working-set tails and
   GPU saturation. Use uninstrumented controls to bound profiling overhead.
3. Pin current merged beta (already including #193), then explicitly qualify the
   chosen current-cue baseline, including the final accepted #197 contracts and
   any #192 integration. If comparing beta with the unmerged #197 candidate,
   label that as a correctness/cost comparison, not a scheduling-only A/B.
   From one identical proof implementation, compare existing scheduling,
   ready-only optional analysis and async with a fixed bounded buffering increment.
   Separately prototype fixed-schedule adaptive admission only if the traces show
   useful slack. Pin Release builds, settings, fixtures, hardware and repeat count.
4. Use deterministic clocks and injected CPU/GPU/worker delays to reproduce the
   retained-tail boundary example, late/cancelled jobs, phase loss and overload.
   Add real capture/playback under pressure; a simulator cannot qualify driver
   scheduling or displayed cadence. Do not change the live install to run a spike.
5. Write the decision with measured benefit, resource cost, latency and cadence
   results, failure modes and remaining uncertainty. Split a selected production
   design into independently testable implementation stories; do not silently
   promote the recovered adaptive patch to the accepted design.

## Acceptance and qualification

- [ ] Explain whether the existing policy is adequate and where it fails; compare
      async and buffered alternatives before choosing an adaptive policy.
- [ ] Define a stable presentation timeline, bounded delay and AV-sync contract;
      analysis admission cannot choose a later display boundary to obtain budget.
- [ ] Preserve exact source/storage/owner/generation/caption context, first-eligible
      adoption, partial-result rejection and current-pixel/model safety checks.
- [ ] Missing or late optional proof never blocks presentation, introduces source
      discards or creates an unintended repeat. Bound queues/memory/workers and
      cancel stale work without unsafe reuse or render-thread joins.
- [ ] Measure foreground current-proof reads/time, optional lookahead time,
      worker service/queue age, duplicate work and first-eligible adoption separately.
      Include direct planar reads and allocation/grouping cost; sample-count
      compliance is not elapsed-time compliance. Re-measure mandatory reserve
      inputs on the selected current-cue implementation.
- [ ] Preserve complete/incomplete/contradictory evidence semantics, private-only
      candidate retention and non-renewing expiry. Verify a late worker, an exact
      repeat, changed pixels/context and a genuine missing cue through production
      coordinator, compositor and finite-cap application, not helpers alone.
- [ ] Keep source pixels and metadata immutable together until actual recovery
      recapture. Distinguish capture sequence, source-time lease, scheduled output,
      physical presentation and wall latency under buffering and backlog recovery.
      Reproduce the morning stall/drop/expiry case without assuming its cause.
- [ ] Qualify normal production thread stack and nested-call pressure, cold/warm
      scratch allocation, owned-job lifetime and bounded memory. Retain #197's
      stack regression and complete configured search/cleanup safety checks.
- [ ] Demonstrate useful ready/adopted proof and transition timing together with
      presentation interval distributions, missed outputs, source continuity,
      intentional delay, CPU/GPU tails and worker/queue/memory pressure. A higher
      completion percentage alone is not acceptance.
- [ ] Cover 23.976/24/25/30/50/59.94/60 Hz and conversions including 24-to-60;
      dark/dense scenes, caption boundaries, format/range variants, full-raster
      return, startup, restart, seek/discontinuity, resize and policy changes.
- [ ] Quantify test conditions and limits. Zero analysis-attributed missed outputs
      and dropped sources in the qualification runs is required; cadence jitter
      must not regress against matched baseline beyond measurement noise. Define
      the measurement tolerance before comparison. Do not claim a universal
      zero-jitter guarantee from finite tests.

## Evidence and recovery

- [Pre-split source](https://github.com/billslack2/videoprocessor/commit/13f43bcb6b99581ffcdc1e800ece471565477a80).
- [Deferred adaptive-policy patch](../assets/VP-0208/deferred-adaptive-policy.patch)
  restores the old policy on the reduced PR commit `acbae401` for experiments.
  It is evidence/recovery, not an instruction to apply it to future beta without review.
- [Actual-class risk probe](../assets/VP-0205/pr193-rebase-20261023/budget-risk-probe.cpp),
  [result](../assets/VP-0205/pr193-rebase-20261023/budget-risk-probe.json), and
  [prior build/test provenance](../assets/VP-0205/pr193-rebase-20261023/validation.json).
- [Prepared-proof controlled measurements](../assets/VP-0205/proof-delivery/README.txt).
- Tracker allocation audit: 226 canonical files and 226 indexed items, maximum
  root 0207, no duplicate/missing/orphan IDs or registry/count mismatch. Allocated
  root 0208, next 0209, total 227. Historical VP-0070 status-heading absence and
  VP-0088/VP-0168 lowercase status are left unchanged; no unrelated tracker repair.
