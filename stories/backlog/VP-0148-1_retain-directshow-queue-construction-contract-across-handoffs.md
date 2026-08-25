# VP-0148-1: Retain DirectShow queue construction contract across handoffs

## Status

Backlog (2026-08-25). Architecture review is complete. The current default
integration branch was discovered as `v1.2.001-beta` at
`2cfbaf2d36a8a848743714178b3fc2861be2d127`; implementation awaits required
developer confirmation of that base.

## Parent

[VP-0148](VP-0148_enforce-directshow-queue-launch-contracts-and-trustworthy-handoff-evidence.md)

## Scope

Make the selected DirectShow queue capacity a revisioned construction
contract. Retain pre-source changes, consume the final value at a defined
construction boundary, record immutable construction evidence, and replace a
stable stale-contract generation at most once.

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
   reset for the stale generation. Existing separately tagged post-start reset
   policy is unchanged.
9. Focused tests, complete native tests and x64 Release host/VP Renderer DLL
   builds pass.

## Boundaries and dependencies

- Depends only on the current beta lifecycle/generation and unified-profile
  mechanisms; extend them rather than adding a competing reset authority.
- Do not claim a particular latency reduction. The incident establishes a
  stale construction contract, not sole latency causation.
- Do not change general queue snapshot, latency-label, wall-clock, NLS or
  display-restoration behavior in this child.
- VP-0148-2 follows this increment and uses its generation/revision identity.
