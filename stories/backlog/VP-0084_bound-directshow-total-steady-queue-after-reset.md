# VP-0084: Bound DirectShow total steady queue after reset

## Status

Backlog. Created from the August 4, 2026 madVR live-playback observation in
which reset completed successfully but reproduced approximately 350 ms of
scheduled latency. The next action is a deterministic queue-policy test that
replays the observed prime, convergence, and steady capture cadence before any
runtime behavior changes.

The creation audit found 98 canonical story files and 98 index rows through
`VP-0083`, with no duplicate or missing IDs. Nine pre-existing index states do
not match their canonical folders: `VP-0034`, `VP-0035`, `VP-0038`,
`VP-0039`, `VP-0040`, `VP-0042`, `VP-0043`, `VP-0062`, and `VP-0075`. This
story does not silently repair those unrelated registry states.

## User story

As a madVR user running a low-latency queue profile, I want a successful reset
to return VP to the configured steady latency instead of rebuilding an
additional raw-frame backlog behind the converted-frame target.

## Observed incident

During live 23.976 Hz playback on August 4, 2026, madVR began running behind
and the OSD showed approximately 350 ms of scheduled latency with two frames
in `R(aw)`. Both the automatic output-readiness reset and the subsequent manual
graph reset executed their lifecycle transaction successfully:

- the automatic reset advanced queue epoch `1 -> 3`;
- the manual reset advanced queue epoch `3 -> 5`;
- `BeginFlush`, `EndFlush`, and `NewSegment` returned success;
- the manual reset health proof passed after approximately 2.0 seconds;
- convergence cleared `raw=2->0`, trimmed `converted=32->3`, and discarded 31
  stale VP frames.

The reset therefore did not fail mechanically. Its practical latency-recovery
purpose failed after fresh input repopulated the queues:

| Point | VP internal | DirectShow PTS lead | Scheduled total | R/C/T |
| --- | ---: | ---: | ---: | ---: |
| Before automatic reset | 102.80 ms | 148.11 ms | 250.91 ms | 0/2/2 |
| After automatic reset | 201.32 ms | 147.84 ms | 349.16 ms | 1/2/3 |
| After manual reset | 201.52 ms | 145.07 ms | 346.59 ms | 1/2/3 |

The OSD also observed raw depth two between telemetry samples. At 23.976 fps,
two frame periods are approximately 83.4 ms, closely accounting for the
roughly 98 ms increase in VP-owned latency. The scheduled lead remained
essentially unchanged and is controlled separately by `lead_frames`.

The active profile was:

```ini
[queue]
queue_size: 32
lead_frames: 4
target_frames: 3
active_picture_lookahead_frames: 2
```

Active-picture look-ahead is not implicated. It schedules semantic geometry
decisions and must not change the live-output reservoir or this fix.

## Root-cause hypothesis

The current DirectShow policy explicitly defines `target_frames` as a
converted-queue target. After convergence, conversion stops when the converted
queue reaches that high-water mark, while capture may retain new raw frames in
addition. A target of three can therefore become three converted samples plus
one or more raw samples and a conversion in flight.

The convergence transaction correctly drops old entries from the front and
retains the newest converted entries. The likely defect is not trim direction
or reset-event signaling; it is steady-state target accounting after reset.
The reset clears the backlog and then deterministically recreates it under the
same converted-only policy.

This hypothesis is strong for the additional VP-owned latency but cannot prove
or measure madVR's private downstream queue. A separate madVR stall may still
exist and must remain distinguishable in telemetry.

## Configuration and queue contract

For the DirectShow live path, an explicitly configured `target_frames` must
bound the total VP-owned *waiting reservoir* in steady state, rather than each
pipeline stage independently. The accounting model must be defined and tested
before implementation:

```text
VP waiting reservoir = raw queued + conversion in flight + converted queued
```

A sample already handed synchronously to the downstream renderer is excluded;
its downstream residence is not observable VP queue occupancy. Startup prime
capacity remains governed by `queue_size` and the established re-prime policy.
`lead_frames` remains a presentation-scheduling offset and is not silently
reduced to make the metric look better.

No new configuration setting is required. Existing profiles remain the A/B
surface: the ordinary `target_frames: 3` profile and the explicitly risky
`target_frames: 1` low-latency profile must both retain their documented
meaning. Help text, trace manifests, and OSD labels must say whether a target
is startup capacity, total steady VP reservoir, or scheduled lead.

## Required implementation

1. Extract or extend a graph-independent steady-reservoir policy that accepts
   raw depth, conversion-in-flight ownership, converted depth, configured
   target, epoch, prime/convergence state, and reset/flush state.
2. Activate total-reservoir enforcement only after the current epoch has
   completed its required prime and convergence transaction. Do not reduce the
   startup prime needed to establish downstream readiness.
3. When steady input exceeds the budget, prefer the newest live source frame:
   discard the oldest excess waiting raw frame before accepting or converting
   stale work. Do not silently discard a sample already owned by DirectShow.
4. Preserve enough converted reserve for continuous synchronous delivery. A
   target of three should normally resolve to a three-frame total waiting
   reservoir, not three converted plus two raw frames.
5. Make capture, conversion, convergence, and reset use the same atomic policy
   snapshot. Queue-depth races must fail toward a bounded extra in-flight frame
   rather than underflow, deadlock, or an unbounded raw backlog.
6. Do not initiate another graph reset merely because normal total-reservoir
   enforcement discarded stale raw input. Keep pressure recovery and lifecycle
   recovery semantically separate.
7. Rewarm latency telemetry after convergence as today, then publish the
   configured target, accounted reservoir, any bounded transient excess, and
   raw/converted discard causes. State-change or periodic logs are sufficient;
   do not add per-frame debug logging.
8. Preserve queue epoch rejection, source-buffer reference ownership,
   discontinuity handling, timestamp sequencing, scene timing, and
   active-picture look-ahead identity.

## Safety boundaries

- Newest-source retention must never reorder frames within an epoch.
- An epoch/reset boundary must still purge all old-epoch ownership.
- A target of one is allowed to be less resilient and may trigger existing
  recovery behavior, but it must not retain an undocumented raw queue behind
  the target.
- If conversion cannot sustain capture cadence, intentional raw replacement
  must be counted and reported; it must not masquerade as successful no-drop
  delivery.
- Alpha's independent presentation queue must not change as an accidental
  side effect. Any renderer-neutral configuration clarification must preserve
  Alpha's established latency behavior.
- No conclusion may be drawn about madVR internal CPU/GPU/render queue depth
  from VP's raw and converted counters.

## Required tests

1. Replay the observed `32 converted + 2 raw -> convergence -> steady cadence`
   sequence with `target_frames: 3`; assert convergence retains the newest
   valid work and steady total VP waiting ownership does not settle at five.
2. Prove an automatic reset and a manual reset clear the old epoch, establish
   the new segment once, pass health proof, and return to the same steady total
   target as a clean start.
3. Cover targets `1`, `2`, `3`, and values near queue capacity with zero, one,
   and multiple raw arrivals during conversion and synchronous delivery.
4. Cover capture/conversion races at the exact high-water boundary and allow
   at most the explicitly documented in-flight tolerance.
5. Prove oldest-raw replacement retains the newest monotonic source sequence,
   releases every discarded source-buffer reference exactly once, and never
   crosses an epoch.
6. Verify startup prime depth and output-readiness evidence remain unchanged;
   total-target enforcement begins only after convergence.
7. Verify `lead_frames` and active-picture look-ahead produce identical
   scheduling/geometry decisions before and after the queue-policy change.
8. Run the full native suite and complete a clean x64 Release build.

## Live validation

Use the same 23.976 Hz madVR content and queue profile that produced the
incident. Record clean start, automatic recovery, and manual reset separately.

For each run, capture epoch, reset reason, flush/new-segment HRESULTs,
convergence trim, raw/converted/in-flight/total depths, source age, VP internal
latency, DirectShow PTS lead, scheduled total, delivery progress, and all drop
causes. Compare at least 30 seconds of stable telemetry before and after reset.

Also repeat with 59.94/60 Hz content and the `target_frames: 1` low-latency
profile. Rapid renderer switching remains a regression case, but the story
must not use renderer reconstruction to hide a queue-policy failure.

## Acceptance criteria

- A successful reset no longer rebuilds a persistent raw backlog outside the
  configured steady target.
- With `target_frames: 3`, the accounted steady VP waiting reservoir is three
  frames, except for a documented and transient in-flight race bounded to one
  frame.
- In the reproduction, post-reset VP internal latency returns within one frame
  period of the clean-start stable median; it does not remain approximately
  100 ms above baseline.
- DirectShow scheduled lead remains governed by `lead_frames` and is reported
  separately from VP queue residence.
- Reset protocol, epoch isolation, output-readiness health proof, continuous
  madVR delivery, and renderer-switch behavior do not regress.
- Any remaining latency spike is truthfully separable as VP-owned residence,
  requested PTS lead, or unobservable downstream behavior.
- The full native suite and clean x64 Release build pass, followed by live
  madVR validation at 23.976 and 59.94/60 Hz.

## Related stories

- VP-0013: DirectShow queue/reset alignment and no-drop review.
- VP-0043: madVR startup and handoff graph re-prime.
- VP-0061: DirectShow in-place reset re-prime with asymmetric madVR queues.
- VP-0066: Live-output pipeline, queue, identity, and epoch architecture.
- VP-0079: Canonical queue profiles and gaming hotkeys.
- VP-0082: Buffered active-picture look-ahead for Alpha and madVR.

## Evidence

- `C:\Videoprocessor\vp\logs\vp_debug.log`, August 4, 2026 session around
  10:23:55 through 10:24:10.
- Source inspection of `EpochBoundedQueue::TrimTo`, DirectShow convergence,
  `LiveSteadyQueuePolicy`, and latency telemetry on the deployed integration
  lineage.
