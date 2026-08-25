# VP-0148-2: Publish coherent DirectShow queue and delivery telemetry

## Status

Backlog (2026-08-25). Depends on acceptance of VP-0148-1. Architecture and
test requirements are recorded; implementation has not started.

## Parent

[VP-0148](VP-0148_enforce-directshow-queue-launch-contracts-and-trustworthy-handoff-evidence.md)

## Scope

Publish one epoch-safe DirectShow queue-ownership snapshot to every diagnostic
consumer and make downstream-delivery duration measure only the downstream
`Deliver(sample)` call.

## Required behavior

1. One versioned snapshot schema contains graph-instance identity, contract
   revision, queue epoch, observation sequence/tick, lifecycle phase, raw
   queued (`R`), conversion-owned (`X`), converted queued (`C`),
   delivery-owned (`D`), optional downstream-call-inflight state, per-stage
   capacities and target/high-water policy. The renderer facade may add GUI
   renderer generation without coupling that UI identity into the pin.
2. `D` becomes one atomically when a sample leaves `C` and remains owned
   through timestamp preparation and downstream `Deliver` until terminal
   success/failure accounting. It is not the current later-starting
   `m_deliveryInProgress`. Define `queued_total=R+C` and
   `vp_owned_total=R+X+C+D`.
3. Acquisition uses bounded seqlock/versioned publication or an equivalent
   nonblocking scheme and returns one complete generation/epoch/observation or
   unavailable after bounded retry. It adds no lock nesting across raw queue,
   converted queue and delivery gate and cannot change pacing or backpressure.
   Each consumer obtains a fresh snapshot; readiness fails closed on
   unavailable/stale, never reads the OSD/log cache, never causes a false reset,
   and cannot starve indefinitely. Consumers never independently sum queue
   getters.
4. Label queued total and VP-owned total distinctly. Do not call a cross-stage
   aggregate `full` by comparing it with one stage's capacity. Serializers
   carry the source snapshot ID and, when given the same observation object,
   emit identical queue fields even though asynchronous consumers need not
   sample the same instant.
5. Bracket only the actual downstream `Deliver(sample)` call with raw QPC or an
   equivalent steady clock immediately before and after it. Scale the delta
   without overflow; do not use or rewrite the known-broken global wall-clock
   conversion. Preparation, completion callback, timestamp/attachment and
   diagnostics use separate duration fields and cannot feed slow-downstream
   classification or convergence.
6. Preserve delivery HRESULT/failure, release/retry/drop, cancellation, epoch
   invalidation and reset behavior.

## Acceptance criteria

1. Deterministic raw-to-conversion-to-converted transfer reports pre
   `R=1/X=0/C=2`, mid `0/1/2`, and post `0/0/3`; VP-owned total remains 3 and
   an impossible mixed-time `1/0/3=4` snapshot cannot occur.
2. Equivalent converted-to-delivery tests report pre `C=3/D=0/owned=3`, both
   preparation and downstream-call states as `C=2/D=1/owned=3`, and
   post-success `C=2/D=0/owned=2` with accepted count advanced exactly once.
   Failure follows existing release/retry/drop behavior without duplicated or
   lost frame identity.
3. Concurrent reset returns wholly old epoch, wholly new epoch, or unavailable;
   no old frame identity appears in a new-epoch snapshot.
4. OSD, logs and trace serializers carry the same source snapshot ID and, given
   one observation object, emit value-identical fields. A 10-minute tagged-
   frame stress has no duplicate ownership, mixed epoch, inconsistent total,
   invalid full/capacity state, deadlock or readiness starvation; acquisition
   remains bounded and does not change pacing or trigger false recovery.
5. With a fake clock, injected 20 ms work before fake `Deliver` changes
   preparation duration but not downstream duration. Injected 20 ms blocking
   inside fake `Deliver` changes downstream duration only. A separate duration
   demonstrably above the configured test threshold changes slow-delivery
   classification/convergence, while the same delay before `Deliver` does not.
   With a synthetic QPC base beyond the old 25.6-hour overflow boundary, a
   +20 ms raw delta still reports 20 ms.
6. At 59.94 and 23.976 Hz with capacity 32 and converted target 3, converted
   depth reaches band 2..3 within `2 * prime_target + 5` successful deliveries
   after buffering release and remains there for 10 seconds, excluding only
   explicitly tagged reset/unavailable intervals. Raw, `X` and `D` remain
   visible but are not folded into the converted-band assertion. Overflow,
   source-gap and policy-induced corrective-reset counts remain zero.
7. Focused tests, complete native tests and x64 Release host/VP Renderer DLL
   builds pass before the root live handoff matrix.

## Boundaries and dependencies

- Depends on VP-0148-1 generation, profile-revision and construction-contract
  identity.
- This child does not redefine `vp_internal`, `pts_lead`, requested-PTS totals,
  madVR native OSD latency readiness, or global QPC conversion. Those require
  separately reviewed stories.
