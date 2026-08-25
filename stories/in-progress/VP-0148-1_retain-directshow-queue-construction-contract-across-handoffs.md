# VP-0148-1: Retain DirectShow queue construction contract across handoffs

## Status

In Progress (2026-08-25). Implementation is committed and pushed on
`codex/vp-0148-1-queue-launch-contract` at `19cf3b2`, based exactly on the
freshly queried remote beta `origin/v1.2.001-beta` at
`2cfbaf2d36a8a848743714178b3fc2861be2d127`. The paired x64 Release host/plugin
was redeployed on 2026-08-25 at 17:53 local time; live madVR handoff/reset
validation remains before completion.

## Progress

- Added one revisioned, lock-linearized DirectShow queue construction contract.
  The exact retained capacity/revision is committed immediately before
  `CLiveSource::Initialize`; immutable constructed evidence cannot be replaced
  by the later current pin capacity.
- Resolved backend-owned queue capacity before renderer construction and passed
  one effective revision/profile generation through all DirectShow constructor,
  fallback, setter, commit and stable-audit paths. Same-effective reapplication
  performs no reset or recreation.
- Added actual downstream-aware allocator request evidence and a shared,
  parseable `commit`/`active`/`mismatch` audit schema with latched profile,
  generation/revisions, desired/retained/constructed/current capacity,
  allocator request/actual, prime/reservoir, estimate satisfaction and explicit
  unavailable values.
- Added latest-wins, generation-gated mismatch handling: one full covered
  renderer recreation is allowed, a repeated same-contract successor mismatch
  terminates without looping, manual Restart remains available, and a backend
  handoff or genuinely new effective contract starts a new recovery lineage.
  No capacity path live-resizes `CLiveSource` or also requests the legacy graph
  reset.
- Live follow-up showed the launch contract was correct but a delayed automatic
  profile reset ran after fresh madVR construction. It changed the settled
  `0/2/2` VP queue shape to `1/2/3`, raised the requested-PTS latency display by
  about 40 ms, and required a user reset to return to the expected epoch. This
  extends this same story from construction correctness through reset
  convergence and provenance; it does not claim madVR scanout latency.
- Fresh Alpha, primary DirectShow, and DirectShow fallback construction now
  consume a matching pending automatic profile reset only when renderer,
  profile snapshot, contract revision, constructed profile generation, and
  immutable DirectShow audit all agree exactly. Rejected submissions retain
  the original profile/source/generation and retry without overwriting newer
  intent.
- Reset reason/priority is now separate from origin. Request, selection, start,
  completion, coalescing, rejection, and retarget-settle logs identify
  `user-manual`, `automatic-profile`, `automatic-readiness`, or
  `automatic-retarget-settle`, with generation and contributor masks.
- Post-reset readiness now validates the exact new queue epoch after delivery
  reopens: full-prime evidence, recent bounded delivery, 16 post-proof
  deliveries, expected raw/converted/retained envelope, and no unclassified
  live-clock gaps. One corrective graph re-prime is allowed per renderer and
  effective contract; a second failed epoch reports manual recovery without a
  reset/recreation loop.
- Deployed madVR validation exposed a false-negative in that new validator:
  two automatic readiness resets reached the expected `0/2/2` VP queue and
  approximately 152--159 ms requested-PTS latency, but validation stayed at
  `stable=0` and incorrectly ended in manual recovery. The epoch-wide maximum
  delivery duration retained the intentional startup hard block, and a healthy
  paced `Deliver()` was commonly sampled while in progress; neither is valid
  post-convergence failure evidence. Both strict checks remain available where
  appropriate: the historical maximum still vetoes adoption of an old graph,
  while actual stalls still fail the current-epoch 500 ms success-age test.
- Post-reset validation now accepts exact, recent current-epoch successful
  progress even when the next paced delivery is in flight. Healthy evidence
  must begin within the two-second acquisition deadline and may use only one
  bounded 1250 ms polling grace to complete its second observation. Late first
  evidence, stale delivery, evidence loss, or unexpected gaps still take the
  bounded corrective/manual path.
- Readiness logs now publish validation blocker bits, stable observation count,
  recent-delivery result, in-progress state, last-success age, and the
  epoch-lifetime maximum delivery duration. Blocker/count changes force a log,
  so `stable=0` no longer hides the rejecting predicate.
- GraphRetarget retains explicit lineage through its required delayed
  LiveQueue E1-to-E2 settle phase. Transient mixed-epoch liveness reads wait for
  the first coherent snapshot; uncredited epoch changes in Prefilling,
  PostResetValidating, or Steady take the bounded corrective/manual path.
- Readiness display-event provenance becomes active only after the exact reset
  operation starts, so a real display event while a coalesced profile reset is
  merely pending still invalidates DXGI measurement. Same-epoch graph-clock
  rollback also starts a fresh latency trend baseline after telemetry rewarms.
- Three independent final architecture gates approved timing/telemetry,
  latency/retry lineage and reset/terminal lifecycle with no remaining blocker.
- Focused contract/race/schema/reset regressions pass; the complete native
  suite is 924/924. The clean-commit x64 Release GUI host and VP Renderer DLL
  builds pass (only the pre-existing libplacebo float-conversion warning
  remains).
- Source commits through `19cf3b2` are pushed to
  `billslack2/videoprocessor:codex/vp-0148-1-queue-launch-contract`.
- Deployed the clean-commit x64 Release `VideoProcessor.exe` and paired
  `VideoProcessorVPRenderer.dll` to `C:\Videoprocessor\vp`. Source/deployment
  SHA-256 hashes match. Active configuration, state, shaders, dependencies and
  shader cache were not modified. The immediately previous binary pair is
  recoverable from
  `C:\Videoprocessor\vp\backups\VP-0148-1-deploy-20260825-175305-19cf3b2`.
  The deployed process restarted successfully as PID 36604 and is responding;
  the new madVR handoff/reset matrix has not yet been performed.

## Parent

[VP-0148](VP-0148_enforce-directshow-queue-launch-contracts-and-trustworthy-handoff-evidence.md)

## Scope

Make the selected DirectShow queue capacity a revisioned construction
contract. Retain pre-source changes, consume the final value at a defined
construction boundary, record immutable construction evidence, and replace a
stable stale-contract generation at most once. A fresh renderer that already
consumed the exact profile must not receive a redundant automatic profile
reset; any reset that does run must have explicit provenance and bounded,
epoch-local convergence validation.

## Required behavior

1. Resolve an explicit selected DirectShow queue profile as a pure input before
   renderer construction; do not seed a new DirectShow generation from a
   visible value owned by the previous backend. Retention below is the race
   safety net, not permission to construct from provisional backend state.
2. `SetFrameQueueMaxSize(N)` validates N and publishes the desired effective
   construction contract plus ordering revision safely. A same effective value
   is a no-op apart from evidence/revision bookkeeping. Before construction
   commit, a changed value is retained for that commit. After commit, a changed
   value is retained only as the next desired contract and must not live-resize
   the stale source; it dispatches the covered recreation in requirement 5
   after the current construction settles.
3. Construction commit is the graph-owner boundary immediately before one
   coherent retained `(capacity, revision)` is consumed for
   `CLiveSource::Initialize` and allocator negotiation. A concurrent setter is
   deterministically before or after that point. The consumed capacity is
   latched as immutable `constructedCapacity`; current pin capacity can never
   substitute for it.
4. Latch construction capacity, later allocator actual, prime/reservoir and
   downstream estimate under the same renderer generation/contract revision.
   Publish phase-tagged `commit`, `active` and `mismatch` evidence with backend,
   profile, desired, retained, constructed, current, allocator request/actual,
   prime/reservoir, estimate, `estimate_satisfied`, and state. Final audit is
   `consistent` only after activation evidence is complete; an unavailable
   estimate is explicit.
5. A newer revision supersedes desired state, but recreation occurs only when
   its effective construction fields differ from immutable constructed
   evidence. Dispatch at most one covered, latest-wins full recreation for the
   stable current-generation contract; never also enqueue the old
   capacity-change graph reset for that stale generation.
6. Mismatch evaluation is suppressed during construction/reset and rejects
   stale generations. A successor that violates the same stable contract
   stops automatic retries, preserves the black transition cover, marks the
   renderer failed, and publishes actionable contract evidence. An explicit
   user restart or genuinely new effective contract may retry.
7. Fresh construction consumes a pending automatic profile reset only with
   exact current renderer/profile/contract/audit evidence. Failed reset
   submission preserves the exact pending identity and retries after debounce.
8. Reset logs keep policy reason/priority separate from origin and identify
   user-manual, automatic-profile, automatic-readiness and automatic-retarget-
   settle requests through selection, execution and completion, including all
   coalesced contributors.
9. Post-reset validation is epoch-local and leaves delivery open once prefill
   is reached. It permits one automatic corrective graph re-prime per renderer
   and effective contract, then requires manual recovery without looping.
10. Readiness measurement preservation applies only while the selected
    readiness Graph operation is actually active. GraphRetarget remeasures and
    carries explicit coverage through its delayed LiveQueue successor.

## Acceptance criteria

1. Construct with 16, set 32 before source creation, then Build: source
   initialization consumes 32 and current allocator policy requests 36.
2. Setter/Build race tests cover both sides of construction commit: before
   commit the revision is consumed; after commit exactly one recreation is
   requested.
3. VP16 to madVR32 activates no DirectShow graph at 16. Desired, retained,
   constructed and current equal 32; allocator, prime/reservoir and downstream
   estimate derive from the same snapshot. For the incident fixture,
   allocator 36 and reservoir 35 satisfy initial estimate 28; other estimates
   use the same formula-based invariant rather than treating 28 as constant.
   Live validation requires `estimate_satisfied=1` for a valid reported
   estimate or explicit `estimate=unavailable`. It proves immutable
   constructed capacity independently of current pin capacity. A graph reset
   preserves the consistent contract.
4. A normal same-contract full madVR recreation preserves revision/capacity 32
   in the successor and requires no corrective second restart.
5. Rapid VP/madVR toggles and profile changes issued during Build and outgoing
   retirement are latest-wins. Tests end once with final intent 16 and once
   with final intent 32; stale generation completions cannot restart either
   successor.
6. Forced mismatch requests one recreation. Repeating the same mismatch in
   the successor records terminal contract failure, retains the cover, permits
   explicit recovery, and never loops.
7. Same-capacity/profile reapply requests neither graph reset nor renderer
   recreation. A complete madVR32 to VP16 to madVR32 round trip restores each
   backend's intended capacity without cross-renderer contamination.
8. A capacity-driven recreation does not also enqueue a capacity-change graph
   reset for the stale generation. A fresh exact construction consumes its
   redundant pending automatic profile reset; independent display-settle and
   retarget-settle resets remain separately tagged.
9. Manual Reset logs `origin=user-manual`. Timer/profile work logs
   `origin=automatic-profile`; readiness and retarget settle use their own
   automatic origins. Coalesced completion retains every contributor without
   relabeling an external display event as reset-generated while merely
   pending.
10. Post-reset E1 reaches Steady only after exact convergence and envelope
    proof. Retarget E1 followed by its covered delayed LiveQueue E2 advances to
    E2 without another Graph reset. An uncredited E2 during Prefilling,
    validation, or Steady takes exactly the bounded corrective/manual path and
    cannot strand or loop.
11. Same-epoch graph-clock rollback rewarms absolute latency and starts a new
    10-second trend lineage; no delta or slope spans the old clock baseline.
12. Focused tests, complete native tests and clean-commit x64 Release host/VP
    Renderer DLL builds pass.

## Boundaries and dependencies

- Depends only on the current beta lifecycle/generation and unified-profile
  mechanisms; extend them rather than adding a competing reset authority.
- Do not claim a particular glass-to-glass latency reduction. VP latency values
  end at the requested DirectShow presentation timestamp and madVR queue/
  scanout remains unobservable. Absolute values rewarm on clock discontinuity;
  trends never span a rebase.
- Do not change unrelated wall-clock, NLS or output-restoration behavior in
  this child.
- VP-0148-2 follows this increment and uses its generation/revision identity.
