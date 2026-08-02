# VP-0076: Decompose the DirectShow live-output pin without behavioral change

## Status

Backlog. Created 2026-08-02 as the independent structural follow-up to
[VP-0066](../review/VP-0066_rearchitect-live-video-output-pipeline.md).
Production extraction must not begin until
[VP-0070-5](VP-0070-5_extract-subtitle-analysis-and-relocation.md) is accepted
and merged because it owns the subtitle-analysis region in the same class.
Before that dependency merges, only baseline, fixture, and test-harness work
that does not edit `CBufferedLiveSourceVideoOutputPin` may proceed.

## User story

As a maintainer of VP's live DirectShow pipeline, I want
`CBufferedLiveSourceVideoOutputPin` reduced to a clear DirectShow adapter and
lifecycle coordinator, so capture, conversion, timing, delivery, and
observability can be maintained and tested independently without changing any
runtime result, timing decision, or data flow.

## Problem and decision

VP-0066 extracted important graph-independent policies and made them directly
testable, but the physical DirectShow pin remains a large orchestration hub.
At the accepted VP-0066 baseline, its implementation is approximately 7,000
lines and still contains a large delivery `ThreadProc`, the conversion worker
loop, timing/sample application, queue convergence integration, scene-cadence
state, pending timestamp history, and trace export.

This story completes the **structural decomposition only**. It must not be
used to optimize latency, correct a timestamp mode, tune queues, redesign
reset/recovery, or otherwise improve behavior. A defect discovered during an
extraction is recorded in a separate story; the affected phase stops and is
reverted until that independent behavior change is accepted.

## Frozen behavior contract

The post-VP-0070-5 Phase 0 baseline is authoritative. VP-0076 must preserve:

- exact presentation and media timestamps, rational remainders, clock domains,
  discontinuities, sample flags, drop/repeat decisions, and PPM application;
- queue count, capacities, targets, order, overflow/stale-discard policy,
  prime/convergence behavior, backpressure, allocator count, and buffer age;
- the existing data path and number of ownership transfers:

```text
DeckLink SDK callback
  -> existing GUI/renderer ingress lease
  -> CLiveSource / CBuffered raw CaptureFrameQueue
  -> existing conversion worker
  -> existing ProcessedFrameQueue
  -> existing CAMThread delivery worker
  -> DirectShow Deliver()
  -> madVR
```

- the same capture callback, one conversion OS thread, one delivery
  `CAMThread`, optional existing subtitle worker, event/wait topology, worker
  handoffs, priorities, wake order, and shutdown order;
- exact DeckLink source-buffer, `IMediaSample`, repeat-clone, allocator, media
  type, COM reference, and destruction ownership on every branch;
- separate authority for `CaptureRunToken`, which rejects callbacks from a
  stale device session, and DirectShow `queueEpoch`, which rejects work from a
  stale graph segment/reset. They must never be collapsed into one generation;
- the state-before-frame ingress contract: DeckLink publishes video state
  before the frame on its SDK thread, the GUI publishes renderer
  acknowledgement under the existing notification mutex, and frame admission
  remains closed until that state is acknowledged. No new queue may buffer a
  frame through an unacknowledged format/EOTF transition;
- epoch creation, startup buffering, readiness, reset, flush, segment,
  renderer-recovery, graph-rebuild, retarget, and shutdown behavior;
- conversion output, pixel hashes, HDR/color metadata, active-picture and
  scene metadata, final sample contents, and rendered result;
- configuration behavior, OSD values, counters, existing log meanings, trace
  fields, units, unavailable states, sampling cadence, and export boundaries.

No new frame copy, queue, thread, worker hop, wait, event, semaphore, polling
loop, sleep, batch, minimum depth, allocator buffer, or per-frame heap
allocation is permitted. No existing lock or atomic ordering may be changed
merely because another design appears cleaner.

The current DeckLink callback is not claimed to be completely nonblocking: it
performs existing metadata/COM work and crosses short existing mutexes. This
story guarantees **no additional blocking or work** on that callback and
measures the frozen and extracted paths under equivalent conditions.
Specifically, it must introduce no wait whose completion depends on progress
by conversion, delivery, allocator, graph control, telemetry I/O, or the
renderer; existing short baseline mutex/COM crossings remain measured and
unchanged.

That consumer-wait guarantee applies to queue-enabled buffered output only.
The existing unbuffered pin intentionally performs allocator conversion and
blocking downstream `Deliver` on the callback chain; VP-0076 preserves that
behavior and adds smoke coverage, but does not make queue-disabled output
asynchronous.

`VideoFrame` is not RAII at this baseline: copying it does not `AddRef` and its
destructor does not `Release`. The buffered pin manually acquires the source
reference at the existing raw-queue boundary. That reference boundary and
every release branch remain exact; no asynchronous handoff may be inserted
before it. Changing `VideoFrame` copy/destructor semantics is out of scope.

## Required final ownership

### `CBufferedLiveSourceVideoOutputPin`

Remains the DirectShow/COM adapter and `CAMThread` trampoline. It owns or
exposes the stable DirectShow boundary:

- pin interfaces and public overrides;
- connection, media-type and allocator negotiation;
- `GetDeliveryBuffer`, dynamic media-type attachment, and downstream
  `Deliver()` adapter calls;
- `Active`, `Inactive`, `Reset`, and destruction entry points, which execute
  adapter-only operations and synchronously delegate the session-owned
  ordering/state transaction;
- `CLiveSource`/`CAMThread`/`CreateThread` operations and fatal downstream-error
  escalation;
- the actual `BeginFlush`, `EndFlush`, and `NewSegment` graph calls; and
- publication to the existing serialized reset coordinator.

It must not contain detailed conversion-loop policy, cadence mathematics,
per-frame delivery policy, pending-timestamp algorithms, or CSV/manifest
serialization. It retains `CAMThread` inheritance; replacing that lifecycle
model is explicitly out of scope.

The pin/base object remains the sole owner of connection/allocator
negotiation, `GetDeliveryBuffer`, pending dynamic-media-type attach/complete,
all `Deliver*`/segment calls, and `CLiveSource`/`m_pFilter`/`m_pLock` lifetime.
Worker/session contexts are non-owning, never receive or cache general pin,
filter, or allocator interfaces outside the narrow pin-owned port described
below, and cannot outlive `Run`. No `CoInitialize`, `QI`, COM activation, or
marshalling is introduced on worker threads. The media-type lock remains
released before downstream `Deliver`, and a pending type completes with the
same result and renderer generation.

### `BufferedLivePipelineSession`

One concrete pin-owned session owns the existing queues, gates, locks, events,
epoch/prime/steady state, worker objects, liveness/latency publication state,
and their destruction order. It never outlives the pin and is destroyed only
after an explicit stop/join. Its destructor does not initiate DirectShow graph
calls; it may assert that workers are stopped and perform non-DirectShow safety
cleanup only.

It is the single owner of **pin-internal** lifecycle/reset state. Graph state,
renderer ingress, and coordinated-reset admission remain responsibilities of
the external owner/coordinator. The session never calls `CBasePin` or
`CBaseFilter` methods directly; the pin continues to perform all base-class,
allocator, media-type, `Deliver*`, flush, and segment adapter operations.

Pin-internal `Reset` preserves this planning-baseline order exactly:

```text
acquire resetTransactionGate
set resetInProgress; invalidate latency; record ResetStarted
set deliveryFlushing
call DeliverBeginFlush before acquiring deliveryGate
acquire deliveryGate
increment queueEpoch before purging old work
purge queues and reset the same timing/prime/cadence/analysis/telemetry state
release deliveryGate
call DeliverEndFlush, then DeliverNewSegment exactly once when permitted
clear deliveryFlushing and resetInProgress with existing result/throw behavior
signal the existing raw/conversion event, then converted/delivery event
CompleteCoordinatedReset; record ResetCompleted; export synchronously
```

No capture or worker component may control the graph or independently replace
the epoch. Pin `Reset` does not add, close, or reopen an admission gate; it
preserves the coordinated-latch versus direct/manual-reset behavior and the
raw queue's second epoch check.

The external coordinator remains outside the session. It closes ingress before
dispatch. Graph reset preserves Stop -> WaitForDrain -> existing 100 ms settle
-> Pause -> pin Reset -> Run -> consume completion -> reopen ingress. Retarget
preserves Stop -> WaitForDrain -> settle -> rebind -> Pause -> pin Reset ->
Run/visible -> completion/reopen. LiveQueue reset calls pin Reset (whose
`BeginFlush` releases blocked downstream `Receive`) before the coordinator's
later WaitForDrain. These observed orders, and the local `Inactive` order, must
be frozen from the post-VP-0070-5 baseline; that observed call graph overrides
this planning snapshot if the accepted predecessor legitimately changes it.
Shutdown preserves close ingress -> stop/flush downstream -> wait for ingress
drain -> teardown; it never waits for a callback blocked in downstream
`Receive` before taking the action that releases that call.

### `DirectShowLiveConversionWorker`

Owns the current conversion-loop and per-thread local state while running on
the **existing** conversion thread. The existing thread entry creates a
concrete, non-owning `Context`/`Services` view of references guaranteed alive
by `Active`/`Inactive`, then calls `Run()` synchronously.

The worker must not own or create a thread, queue, event, allocator, callback
graph, or sample copy. It preserves wait order, allocator acquisition,
formatter/`FrameProcessor` invocation, analysis order, epoch recheck,
source-buffer release, processed publication, signals, counters, sleeps, and
all failure paths exactly.

### `DirectShowLiveDeliveryWorker`

Owns the current delivery loop and its per-epoch local state while running on
the existing `CAMThread`. It remains the only worker that calls downstream
delivery and the sole committer of delivery-owned Rational-Rational
presentation/media state.

It preserves converted-queue reserve/pop behavior, initial prime observation,
timing-controller/sequencer interaction, convergence/high-water policy,
sample preparation, delivery result handling, repeat ownership, source-gap
accounting, counters, and nonblocking reset publication. It does not own the
graph, create a thread, change an epoch, or perform flush/segment operations.

The pin owns one concrete `DirectShowPinDeliveryPort`. It may internally hold
one lifetime-bounded non-owning pointer/reference to its owning pin solely to
forward the existing `GetDeliveryBuffer`, dynamic-media-type attach/complete,
`Deliver`, and reset-request operations. Conversion receives the buffer-
acquisition/repeat-clone subset; delivery receives the delivery subset. Worker
contexts retain only a non-owning reference to this concrete, non-virtual port,
never store or access a pin pointer, perform no `AddRef`/`QI`/marshalling, and
cannot outlive `Run`. The port cannot escape to another owner and contains no
policy, queue, thread, epoch, or per-frame allocation.

### `TimingSampleApplicator`

Owns mechanically extracted mutable timestamp/sample-preparation operations
without unifying timestamp modes or moving work between threads. Legacy
Clock/Smart preparation that currently occurs during conversion stays on the
conversion thread. Rational-Rational preview and stamp remain on the delivery
thread. The applicator exposes preview/stamp and commit/abort operations but
never calls `Deliver`. `DirectShowLiveDeliveryWorker` alone preserves the exact
Preview -> SetTime/SetMediaTime/flags -> begin media-type transaction ->
`Deliver` -> complete media-type transaction -> Commit sequence. Preview stays
side-effect free; Commit occurs only after `S_OK`; `S_FALSE` or failure invokes
Abort/no-commit and does not advance sequencer state.

Pending/delivered timestamp rings, source gaps, recycled-sample flag clearing,
media-versus-presentation repeat semantics, reset behavior, and exact clock
domains remain unchanged.

### `PendingTimestampHistory`

Owns the existing timing-critical fixed ring, sequence, tolerance,
synchronization, lookup, and reset behavior. It is not passive telemetry and
remains on the exact conversion/delivery/reset call sites and threads frozen in
Phase 0.

### `LiveSceneCadenceController`

Owns the existing delivery-thread scene cadence state, phase accumulation,
forecast/planning state, pending upstream-repeat sample, advanced media-time
offset, and preview/commit behavior. It consumes existing frame/timing
evidence and returns the same decision. It owns no queue, graph, reset, or
downstream delivery operation.

### `LiveOutputTelemetry`

`LiveOutputTelemetry` owns the existing bounded event, convergence, and
periodic metric traces; liveness/latency publication; run identity; export
ordinal; and CSV/JSON/manifest serialization.

Critical-path recording remains bounded and memory-only. Snapshot and file
serialization occur synchronously on the same reset/inactive owner and at the
same boundaries as the baseline. No telemetry thread is added.

Existing reset-trace export remains synchronous on the reset owner after the
workers are woken. This story measures reset-to-first-delivery but neither
adds export work nor moves export I/O onto capture, conversion, or delivery;
decoupling that export requires a separate behavior story.

Existing telemetry fields, schemas, log text/meaning, counters, sampling
cadence, throttling, unavailable states, and export boundaries cannot be added
to, removed, renamed, or reinterpreted by VP-0076. Extraction only relocates
existing operations without changing their call sites or critical-path
behavior. Missing equivalence evidence uses an external harness or test-only
instrumentation absent from the final Release binary and shipped schemas.

Workers receive a concrete non-owning context and the narrow port reference,
not a general pin back-pointer, `std::function`/virtual callback graph, or
per-frame service allocation. Constructors do not start workers, and
destructors do not make DirectShow calls.

## Implementation phases

Each named ownership transfer is a separate buildable, testable, and
revertible checkpoint. No checkpoint may depend on a later extraction to
restore behavior or pass equivalence. Phase 4 separates timestamp history,
sample application, and cadence checkpoints. Phase 5 moves one resource/state
family at a time: queues/locks/events, then epoch/prime state, then lifecycle
delegation. Later checkpoints are reverted first when rolling back an earlier
dependency.

### Phase 0: Freeze the post-VP-0070-5 baseline

- First record and tag the pristine accepted post-VP-0070-5 integration
  baseline. Add only a characterization harness and test-only/additive
  measurements, prove that build behavior-equivalent to the pristine tag, then
  tag it separately as the Phase-0 measured comparison baseline. Every later
  extraction compares against the measured baseline; the pristine tag remains
  provenance. Neither instrumentation nor new schema enters the final Release.
- Complete a clean x64 Release build and full native suite.
- Archive paired executable/renderer hashes and the active configuration hash
  without replacing the user's configuration.
- Capture deterministic fixtures for every timestamp mode, queue operation,
  pixel/metadata result, sample flag, ownership path, and reset sequence.
- Record test-session metadata with the source commit, executable and renderer
  hashes, configuration hash and parsed queue/timestamp values, scenario/run
  ID, operator display class, madVR CPU/GPU/present settings, and result
  annotation. madVR fill remains an explicit operator observation because
  occupancy is unobservable.
- Capture retained-buffer age, reference balance, callback duration, and trace
  collision evidence through existing diagnostics where available. Missing
  evidence uses an identically instrumented baseline/new test build or external
  harness and is absent from production source paths and final Release output.
- Record fast-monitor and slow-Epson live references, the thread/event/lock
  topology, atomic orderings, queue/copy/handoff counts, and responsibility/
  line map.
- Add a controlled fake allocator/sample/downstream harness with reference
  counting and DirectShow call-order recording before moving production code.

No production extraction begins until this evidence is reproducible.

### Phase 1: Extract passive observability

Extract `LiveOutputTelemetry` snapshot/export serialization first.

- Golden headers, deterministic rows, manifest types/units, OSD snapshots,
  export thread, and boundary match after masking only declared run identity,
  path, wall-clock, and build fields.
- No capture/conversion/delivery file I/O or added hot-path work.
- This phase remains independently revertible.

### Phase 2: Extract the conversion worker

Move the current loop mechanically into
`DirectShowLiveConversionWorker::Run` while retaining the existing thread
creation and entry trampoline.

Fault-injection tests cover acquire, conversion, stale epoch, overflow,
failure, reset during conversion, source release, processed publication,
signal, and shutdown ordering. Pixel hashes, metadata, analysis flags,
callback counts, queue operations, wait behavior, and reference balance match
exactly.

### Phase 3: Extract the delivery worker

Move `ThreadProc` mechanically into `DirectShowLiveDeliveryWorker::Run` while
cadence, timestamp history, and sample application remain inline. Do not
change execution threads or the DirectShow transaction.

Controlled downstream tests cover blocked `Deliver` with concurrent
`BeginFlush`, `S_OK`, `S_FALSE`, failures, stamp failure, repeat clone, reset
race, stale epoch, convergence, dynamic media type, and exact reference/call
order. No raw/processed/state/allocator/media-type lock may be held through
`Deliver`; only the existing narrow delivery gate may span it.

Each raw item owns exactly one DeckLink source-buffer reference, each processed
item exactly one `IMediaSample` reference, and each repeat clone an independent
sample reference. Success, failure, stale, flush, and reset branches preserve
the exact release order and count.

### Phase 4: Extract timing/sample and cadence policy

Extract `PendingTimestampHistory`, `TimingSampleApplicator`, and
`LiveSceneCadenceController` as three separate checkpoints from inside the
already-isolated worker boundary. Pending history receives exact all-mode
lookup/reset replay. Sample application preserves thread affinity and the
complete delivery transaction. Cadence replay covers correction off, basic,
upstream drop/repeat, incompatible rate, warm-up, failed delivery, reset, and
generation replacement. Presentation/media values, decisions, reference
ownership, and failure non-commit behavior match exactly.

### Phase 5: Consolidate lifecycle and epoch state

Move resource/state ownership into `BufferedLivePipelineSession` last. Preserve
the exact baseline order for successful and partially failed `Active`,
repeated `Active`/`Inactive`, every reset requester, blocked downstream,
media-type/HDR rebuild, shutdown, and destruction.

The conversion thread starts first, then delivery, then the optional existing
subtitle worker. The external owner closes admission before graph shutdown.
Pin `Inactive`, under the existing pin lock and with the frozen base-class call
location, sets the pin inactive, publishes inactive/clears prime, signals the
existing events, joins conversion, stops/joins subtitle, `Close`s/joins
delivery, exports, purges, and resets events in the frozen order. Constructors
start no thread; export and purge occur only after workers cannot touch state;
the session destructor performs no DirectShow calls.

Failure acceptance is exact: `BeginFlush` failure does not acquire the delivery
gate or call `EndFlush`/`NewSegment`; a state-transition exception attempts
`EndFlush` then preserves the baseline throw path; `EndFlush` failure suppresses
`NewSegment`; and `NewSegment` failure preserves current flags, latch, and wake
behavior. A blocked-`Deliver` test proves `BeginFlush` precedes waiting for the
delivery gate, and no stale sample crosses the segment.

### Phase 6: Slim the adapter and complete live A/B validation

Remove obsolete forwarding state and document the final lock, lifetime,
thread, reset, and ownership graph. Structural acceptance is responsibility-
based, supported by these non-blocking diagnostics:

- the pin implementation is reduced by at least 60% from the frozen
  post-VP-0070-5 baseline and is approximately 1,500 lines or fewer;
- its header is approximately 300 lines or fewer;
- `ThreadProc` and `ConversionWorker` are same-thread delegation shells of at
  most approximately 40 lines each; and
- no extracted component merely becomes a differently named untestable
  monolith.

Line counts do not override behavioral equivalence. If reaching a size target
would require a semantic change, equivalence wins and the residual ownership
is documented.

## Automated acceptance matrix

Tests cover:

- 60000/1001 and 24000/1001, SDR and HDR;
- every existing timestamp mode, including preserved known limitations;
- queue targets 0, 1, 2, 4, and capacity-bound values; lead values 0 and 1;
- prime, steady delivery, overflow, stale work, downstream stalls, and
  convergence;
- scene correction off/basic/upstream drop/upstream repeat;
- capture gaps, counter rollback, media-type/EOTF change, and epoch replacement;
- `CaptureRunToken` and `queueEpoch` advancing independently and rejecting only
  their respective stale callback/work classes;
- state-before-frame ordering across format/EOTF publication, renderer
  acknowledgement, and frame admission;
- queue-disabled/unbuffered output retaining its synchronous callback-chain
  behavior;
- manual/output-readiness/liveness reset, renderer recreation, retarget,
  inactive, shutdown, and partial activation failure;
- reset while conversion is in flight and while `Deliver` is blocked;
- allocator, conversion, `Deliver`, `BeginFlush`, `EndFlush`, and `NewSegment`
  failures;
- existing frame `GetBytes` failure and profile/notification exception branches
  receiving direct-call fake-dependency smoke coverage where already reachable,
  without a production injection hook or allowing an exception to cross a real
  SDK/COM callback boundary;
- recycled sample flags, source/sample/repeat reference balance, and worker
  join-before-destruction;
- exact four-hour timestamp simulations;
- deterministic telemetry requiring exact record count, order, schema, and
  fields after masking declared identity fields; trace/snapshot/export
  equivalence; and
- repeated concurrent transition stress with bounded completion time.

The full native test count must never decrease.

## Exact and quantitative equivalence guardrails

Deterministic fixtures require exact equality for:

- queue operations/depth after every modeled event;
- presentation/media timestamps, cadence decisions, and correction counters;
- epoch/reset state and DirectShow call order;
- sample flags, media types, pixel hashes, metadata, and error results;
- ownership/reference counts; and
- controlled structured telemetry with exact record count, order, schema, and
  semantic values after masking only declared run identity, path, build, and
  wall-clock fields. Concurrent live trace collisions may vary naturally, but
  buffer capacity, collision behavior, and export semantics remain identical,
  and measured loss does not exceed the Phase-0 distribution.

Performance comparisons use interleaved baseline/new runs on the same machine,
configuration, input, renderer, display, and power state. Report every cycle
and the median of at least five matched cycles. Synthetic callback tests use
at least 100,000 invocations for p99.9. Live steady-state latency excludes
explicitly reported buffering/reset intervals and uses at least ten minutes per
core cell. First-current-epoch successful delivery is measured from the first
valid admitted frame or graph-ready boundary; operator-visible first picture,
remote-command latency, and physical HDMI acquisition are reported separately.

Matched runtime validation requires:

- identical queue/thread/handoff/event/semaphore/wait/sleep/lock/copy counts,
  allocator count, constants, and atomic semantics;
- no new per-frame heap allocation or file/formatting operation;
- for both ordinary and video-state-change frames, capture callback
  entry-to-return and callback-to-raw-admission p99 and p99.9 increase no more
  than `max(10% of baseline, 0.1 ms)`, with no increase in missed frames;
- capture-arrival-to-`Deliver` median and p95 no more than 1 ms above baseline;
- conversion median and p95 no more than 2% or 0.2 ms worse, whichever is
  larger;
- first-current-epoch successful delivery, operator-visible first picture,
  reset recovery, and target convergence within one nominal frame interval of
  baseline and never beyond the existing accepted 15-second recovery ceiling;
- no steady queue-depth increase, new sustained VP/madVR drop or repeat slope,
  additional reset/rebuild, epoch churn, deadlock, leak, stale delivery, or
  use-after-free;
- retained DeckLink source buffers never exceed raw capacity plus the one
  conversion frame in flight and drain to zero after stop; and
- median process CPU does not increase by more than 5% relative or two
  percentage points absolute; exceeding either bound fails.

A deterministic mismatch is a failure and cannot be normalized away as
measurement noise.

## Live validation

Use paired x64 Release builds and preserve the active configuration. Compare
at least five frozen/new cycles for every quantitatively gated/core transition;
focused smoke-only cells may use three. Cover the fast monitor and slow Epson,
including:

- 59.94 SDR, 23.976 HDR, and 59.94 HDR;
- Rational-Rational and Clock-Smart2 deeply, plus smoke tests for every other
  timestamp mode without fixing their known behavior;
- symmetric and asymmetric madVR CPU/GPU queues, including 6/12 and 16/8;
- target 0/1/2/4 and lead 0/1;
- cold start, manual reset, automatic reset, Apple TV/app/channel/source
  transition, SDR/HDR/rate change, madVR restart, renderer switch,
  fullscreen/window retarget, capture stop/start, graph rebuild, and shutdown
  during active playback; and
- madVR and Alpha smoke coverage, with madVR used for the deep DirectShow
  boundary matrix.

Retain VP traces/logs, configuration and binary hashes, and passive madVR OSD
evidence. After every critical monitor/Epson transition, the configured VP
target semantics and steady R/C/T envelope match baseline; madVR decoder,
upload, render, and present queues visibly return to their configuration-
specific frozen full range within the baseline recovery envelope and no later
than 15 seconds, remain filled, and show no sustained drop/repeat slope. This
is operator evidence only: occupancy remains unobservable and is never a VP
control input.

Ten-minute steady validation is required for these core cells: fast-monitor
59.94 SDR Rational-Rational, Epson 59.94 SDR Rational-Rational, 23.976 HDR
Rational-Rational, 59.94 SDR Clock-Smart2, and the two asymmetric madVR queue
cases. Targets and lead values use pairwise coverage across those cells. Other
timestamp modes, renderer switches, target/lead combinations, and failure
transitions receive focused smoke coverage. Transient bursts must be no worse
than baseline; merely eventually settling is insufficient.

## Rollback and review process

- Implement one independently buildable and revertible phase per commit
  series.
- The complete native suite and phase-specific equivalence tests pass before
  senior DirectShow, capture, and regression review at each phase.
- Live-smoke each worker extraction before moving lifecycle ownership.
- Keep the integration/beta branch unchanged until the complete branch passes
  acceptance.
- Back up and deploy `VideoProcessor.exe` and
  `VideoProcessorVPRenderer.dll` as one paired x64 Release runtime; preserve
  configuration with minimal, backed-up edits only when explicitly required.
- On any unexplained mismatch, stop and revert the most recent phase. Do not
  tune timing, queue, or reset behavior inside VP-0076 to make a comparison
  pass.
- Do not retain dual production paths or a long-lived runtime switch. Golden
  fixtures and Git history are the rollback/reference mechanisms.

## Dependencies and sequencing

- VP-0066 supplies the stable, testable policy seams and accepted live-output
  behavior.
- VP-0070-5 is a hard dependency and must merge first. VP-0076 consumes its
  narrow subtitle/analysis interface and does not redesign or re-inline it.
- No parallel production work may edit
  `CBufferedLiveSourceVideoOutputPin`. If another DirectShow, timing, or
  lifecycle change lands first, Phase 0 is deliberately repeated against the
  new integration baseline rather than silently reusing old fixtures.

## Explicit exclusions

- Latency optimization, queue reduction, or configuration/default changes.
- Timestamp, PPM, presentation-lead, frame-offset, cadence, source-gap,
  readiness, convergence, reset-threshold, or recovery changes.
- Fixing unsupported or defective timestamp modes.
- Investigating or changing exception behavior at the COM callback boundary;
  a no-throw ABI correction is a separate safety increment.
- Scene, active-picture, OCR, subtitle, relocation, conversion, pixel, shader,
  HDR, range, primaries, metadata, renderer, UI, or OSD behavior changes.
- madVR occupancy inference or new madVR control APIs.
- DirectShow replacement, C++ language upgrade, Alpha redesign, lock-free
  redesign, generic thread pool, async trace writer, or opportunistic cleanup.
- Renaming/removing existing telemetry fields, logs, counters, or meanings.

## Stop conditions

Stop the phase and request a separate story or explicit scope decision if the
extraction appears to require:

- a different timestamp/cadence decision, sample/pixel/metadata result, or
  queue operation/target;
- a new wait, thread, queue, copy, allocation, lock boundary, event, poll, or
  sleep;
- a changed reset/flush/segment/worker/ownership order;
- a change to an existing telemetry schema or semantic; or
- a workaround for an existing behavioral defect.

No such change is authorized by VP-0076.
