# VP-0148: Enforce DirectShow queue launch contracts and trustworthy handoff evidence

## Status

Backlog (2026-08-25). Incident and source analysis are complete. Three
independent architecture reviews approved the bounded decomposition below.
The repository default/integration branch was manually discovered as
`v1.2.001-beta` at `2cfbaf2d36a8a848743714178b3fc2861be2d127`.
Implementation awaits the required developer confirmation of that base.

## User story

As a VideoProcessor operator, I want every DirectShow/madVR renderer generation
to start with the selected queue contract and report one coherent view of its
queue ownership, so renderer handoffs cannot silently retain construction
state from the prior backend and queue or downstream-delivery diagnostics do
not imply precision they do not have.

## Incident evidence

- During repeated VP Renderer/madVR handoffs on 2026-08-25, the selected madVR
  profile requested queue capacity 32, but one madVR generation was constructed
  with the prior visible capacity 16. Its allocator used 20 buffers and its
  launch reservoir was 19 against a 28-frame downstream estimate, so the log
  reported `estimate_satisfied=0`.
- Graph resets retained that renderer generation and therefore could not
  renegotiate construction-time allocator state. A full renderer recreation
  constructed capacity 32, allocator 36, reservoir 35 and
  `estimate_satisfied=1`.
- VP-side residence subsequently fell by approximately 38--42 ms at 23.976 Hz,
  but this is correlation, not proof of sole causation: an earlier
  capacity-16 madVR generation also reached approximately 125 ms after reset.
  Acceptance must prove the launch contract and bounded queue ownership, not a
  fixed glass-to-glass latency reduction.
- VP Renderer depth 8 was expected for its target 9 policy. DirectShow target
  3 intentionally permits converted depth 2 after dequeue. The displayed
  `1/3/4` tuple was not trustworthy as one instant because raw and converted
  depths were sampled independently while ownership could transfer between
  them.

## Confirmed root causes and measurement defects

1. `DirectShowVideoRenderer::SetFrameQueueMaxSize` posts to the graph owner but
   returns when `m_liveSource` does not yet exist, without retaining the new
   capacity. `LiveSourceBuildAndConnect` later initializes the source from the
   stale constructor member.
2. Profile resolution currently follows renderer construction. The visible
   queue value can therefore originate from the prior backend before the
   selected DirectShow profile is applied.
3. Current pin capacity is not construction truth. A post-connect change can
   report current capacity 32 while allocator and launch-reservoir resources
   remain negotiated for 16.
4. OSD/log queue components are read separately and omit explicit conversion
   and delivery in-flight ownership, permitting a mixed-time total.
5. The downstream-delivery timer starts before preparation/diagnostic work and
   ends after `Deliver`, so it can classify VP work as downstream blocking and
   influence convergence decisions.

## Architectural contract

- Resolve the final DirectShow queue profile before construction whenever
  possible. The renderer must also retain a validated requested capacity and
  monotonically identified contract revision so a pre-source setter call
  cannot be lost.
- Define an explicit construction-commit boundary. Atomically consume one
  retained `(capacity, revision)` pair, pass it to source initialization, and
  preserve immutable construction evidence for that renderer generation.
- Treat desired profile capacity, retained requested capacity,
  construction-time consumed capacity, current pin capacity, allocator
  request/actual, prime target/reservoir and downstream estimate as distinct
  fields. Never infer construction consistency from current pin capacity.
- A stable current-generation mismatch after construction is not repairable by
  graph reset. Dispatch at most one covered, latest-wins full renderer
  recreation for the stable contract. If its successor violates the same
  contract, fail visibly and do not loop.
- Publish queue ownership through one schema and bounded, nonblocking,
  generation- and epoch-consistent acquisition. OSD, periodic logs, traces and
  readiness use fresh snapshots from that producer; readiness never consumes a
  cached diagnostic sample. Distinguish queued total from total VP-owned work
  and include conversion-owned and delivery-owned work.
- Time the downstream `Deliver(sample)` call directly. Report preparation and
  diagnostics separately so logging stalls cannot become downstream-delivery
  stalls or convergence inputs.

## Decomposition

1. [VP-0148-1](VP-0148-1_retain-directshow-queue-construction-contract-across-handoffs.md)
   -- Resolve, retain, commit and verify the DirectShow construction contract,
   with generation/revision guards and one-shot recreation. This is the first
   production increment.
2. [VP-0148-2](VP-0148-2_publish-coherent-directshow-queue-and-delivery-telemetry.md)
   -- Replace mixed-time queue totals and contaminated downstream timing with a
   coherent ownership snapshot and Deliver-only duration. It follows
   VP-0148-1 so the final live matrix can validate both contract and evidence.

The root closes only after both children are accepted and the repeated live
handoff matrix records stable contract and ownership evidence. The root
normally remains Backlog while a child is active.

## Cross-task acceptance criteria

1. At both 59.94 and 23.976 Hz, the first running madVR graph uses the final
   selected profile revision. For the incident profile, desired, retained,
   constructed and current capacity are 32; allocator request/actual,
   prime/reservoir and downstream estimate all derive from that same contract
   snapshot; no provisional capacity 16 is activated for that generation. In
   the recorded incident fixture, allocator 36 and reservoir 35 satisfy the
   initial estimate 28, without making 28 a universal madVR constant.
2. VP Renderer/madVR/VP Renderer is repeated 20 times. Every switch has one
   current renderer generation, the latest profile wins, stale work cannot
   restart a successor, and no automatic graph reset or restart occurs merely
   to apply an already-consistent contract. Fresh exact construction consumes
   a redundant pending automatic profile reset; independent display/readiness/
   retarget recovery remains generation-bound and separately tagged.
3. A stable construction mismatch causes exactly one covered renderer
   recreation. A persistent same-contract mismatch becomes an actionable
   terminal failure without a restart loop.
4. Coherent snapshots cannot duplicate a frame during raw-to-conversion,
   conversion-to-converted, or converted-to-delivery handoff; a reset returns
   wholly old epoch, wholly new epoch, or unavailable.
5. Injected delay before `Deliver` changes preparation time only; injected
   blocking inside fake `Deliver` changes downstream duration and the existing
   slow-delivery classification. Failure/HRESULT behavior is unchanged.
6. Queue residency, overflow/source-gap counters, generation/profile identity
   and policy-induced reset behavior are the pass criteria. VP requested-PTS
   totals and madVR OSD latency remain secondary observations, not
   glass-to-glass acceptance values.
7. The complete native suite and x64 Release host/VP Renderer DLL build pass
   before live validation or deployment consideration.

## Boundaries and follow-up findings

- DirectShow/madVR launch capacity and its evidence are in scope. VP Renderer
  target-9/healthy-8 semantics and unrelated target, lead, lookahead, NLS,
  reset-coordinator priority or display-change behavior are unchanged.
- Do not use a graph reset to repair construction-time allocator mismatch.
- General latency relabeling and adjacent clock-pair correction are separate
  higher-coupling work. Current `vp_internal + pts_lead` reaches requested PTS,
  not actual presentation or glass-to-glass latency, and its component samples
  have a small overlap.
- madVR native OSD latency warm-up/qualification is separate. VP-0148 must not
  use it as a pass/fail value.
- `GetWallClockTime` has an urgent separate signed-multiplication overflow near
  25.6 hours uptime at 10 MHz QPC. This story must not silently expand into a
  global clock rewrite.

## Related work

- VP-0134, verified symmetric renderer handoff and display restoration
- VP-0137, bounded madVR queue, NLS and renderer-switch behavior
- VP-0143, restart after queue-profile changes
- VP-0146, safe queue-profile handoff and rule re-application

## Tracker audit and readiness review

- After fetching `origin/main`, the audit found 164 canonical files and 164
  index rows, highest root `VP-0147`, no duplicate or missing IDs, and registry
  totals consistent. A pre-existing unrelated status-capitalization mismatch
  remains in VP-0088 (`In progress` versus `In Progress`) and was deliberately
  not changed as part of this story.
- Configuration ownership, pipeline order, UI/graph threading, construction
  lifetime and the validation seams are known. No unresolved API or platform
  assumption invalidates this decomposition.
- Source work must begin from a clean worktree at the confirmed current remote
  beta tip. No local checkout or deployed commit is an integration baseline.
