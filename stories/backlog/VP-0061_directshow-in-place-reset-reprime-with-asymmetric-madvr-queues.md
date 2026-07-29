# VP-0061: DirectShow in-place reset re-prime with asymmetric madVR queues

## Status

Backlog.

Investigation on July 29, 2026 reproduced and isolated the failure. The current
implementation base discovered from GitHub is `v1.1.015-beta`; developer
confirmation of that base is required before implementation begins.

Tracker audit at creation found 59 canonical story files and 59 index rows,
with no duplicate or missing IDs. The existing index states for VP-0034,
VP-0035, VP-0038, VP-0039, VP-0040, VP-0042, and VP-0043 do not match their
canonical `done/` locations. Those pre-existing mismatches were reported and
were not silently repaired as part of this story.

## User story

As a VideoProcessor user with valid asymmetric madVR CPU/GPU queue sizes, I
need Reset to flush and re-prime the existing DirectShow renderer reliably,
without becoming a renderer Restart and without adding steady-state VP
latency.

## Observed failure

With madVR CPU/GPU queues set asymmetrically, such as 16/8, and the VP queue
capacity set to 64:

- a newly created madVR renderer fills and runs normally;
- a manual Reset can leave madVR's decoder/upload/render queues near one;
- VP then backs up while the existing madVR filter stops consuming;
- a full renderer Restart recovers;
- equal madVR queue sizes such as 8/8 or 16/16 mask the failure.

The deployed July 29 log accounts for every sample in the failing epoch:

```text
111 input = 81 converted + 30 raw
81 converted = 16 delivered + 1 blocked Receive + 64 converted-queue samples
```

The 17th synchronous delivery blocks exactly after madVR accepts its configured
16-frame CPU queue. Capture and conversion remain healthy.

## Root-cause hypothesis

VP's current graph reset performs:

```text
graph Stop
source BeginFlush / EndFlush / NewSegment while stopped
graph Run
```

This violates the ordering required for a push-source discontinuity. The
downstream `BeginFlush` must occur before the source streaming path is halted
so a blocked `Receive` and retained renderer samples are released. `NewSegment`
must be serialized with streaming after workers/pins resume and before the
first discontinuous sample.

Asymmetric madVR queues are supported and are a diagnostic trigger, not an
invalid configuration or evidence that settings changed while the graph was
running.

## Readiness review

- Configuration model: no new configuration is required. Existing madVR and
  VP queue settings remain authoritative.
- API behavior: DirectShow's push-source reset order is documented as
  `BeginFlush`, halt streaming, `EndFlush`, restart streaming, `NewSegment`,
  then a discontinuous first sample.
- Pipeline order: capture, raw queue, conversion, converted queue, synchronous
  DirectShow delivery, and madVR's internal CPU/GPU queues are identified.
- Resource lifetime: Reset must retain the graph, madVR filter, negotiated
  media type, allocator, renderer window, and shader state. Restart remains a
  separate explicit lifecycle operation.
- Platform boundary: madVR is closed-source, so correctness is established by
  the DirectShow protocol, observable delivery progress, queue behavior, and
  repeated live-HDMI validation.
- Validation boundary: automated tests prove ordering/state invariants; x64
  Release build proves integration; live HDMI with madVR 16/8 proves downstream
  behavior and latency.
- Worktree requirement: implementation must use a clean worktree based on the
  developer-confirmed integration branch.

No unknown currently requires a separate spike. The first implementation step
is deliberately diagnostic: compare the existing running-graph flush with the
current Stop/Run reset before committing the final two-phase transaction.

## Required implementation

1. Preserve the semantic distinction between Reset and Renderer Restart.
2. Gate new delivery for the reset epoch.
3. Send downstream `BeginFlush` while the graph/pins are active.
4. Quiesce conversion and delivery workers at a bounded epoch barrier.
5. Purge raw, converted, and in-flight old-epoch samples and reset timing,
   cadence, frame, and PPM state.
6. Send `EndFlush`.
7. Resume workers without rebuilding madVR.
8. Deliver exactly one `NewSegment(0, MAXLONGLONG, 1.0)` from the serialized
   streaming path before the first new sample.
9. Mark the first new sample discontinuous.
10. Keep a full renderer Restart only as a bounded failure fallback when an
    in-place flush cannot complete safely.
11. Remove reliance on an arbitrary sleep for reset correctness.

If Reset retains an `IMediaControl` state transition, log and validate exact
HRESULTs and intermediate states rather than treating every successful
HRESULT, including `S_FALSE`, as completed running state.

## Latency contract

The change must not increase steady-state VP buffering, queue targets, frame
offset, or source-to-renderer latency.

- No additional frames may be retained after reset completion.
- Existing VP buffering targets remain unchanged.
- Any increased total latency caused by deliberately larger madVR queues is
  external to VP and acceptable.
- Reduced reset blackout/re-prime time is desirable but cannot be obtained by
  dropping unaccounted frames or bypassing the discontinuity boundary.

## Validation

### Automated and build validation

- Add focused tests for reset phase ordering, one `NewSegment` per epoch,
  discontinuous first sample, stale-epoch rejection, and Restart remaining a
  distinct path.
- Verify no queue target or steady-state latency constant changes.
- Run the native test suite.
- Build the complete solution as x64 Release.

### Live-HDMI matrix

| Scenario | Required result |
| --- | --- |
| madVR 16/8, VP 64, 20 manual Resets | Every reset advances beyond 16 successful deliveries within one second and returns to approximately 60 fps |
| madVR 8/8 and 16/16 controls | No regression |
| madVR 24/16 and 32/8 stress cases | No CPU-queue-plus-one delivery block |
| Reset during low and full madVR queues | Same successful re-prime |
| Renderer Restart | Still rebuilds the renderer and remains independently functional |
| Stable playback before/after reset | No added VP queue depth, offset, drops, or measured latency |

Record for each run: reset epoch and phase timings, exact graph state/HRESULT,
raw and converted depths, capture/conversion/delivery counters and ages,
blocked-delivery state, first-new-segment time, first-new-frame time, dropped
frames by cause, VP latency, and DirectShow latency.

Temporarily disabling madVR's “delay playback start until render queue is full”
is permitted only as a diagnostic confirmation of the preroll interaction, not
as the product fix.

## Acceptance criteria

- Manual Reset works repeatedly with asymmetric madVR queues while retaining
  the same renderer instance.
- The 17th-delivery stall is absent at madVR CPU queue size 16.
- Reset follows a documented, serialized DirectShow flush/new-segment
  transaction.
- Renderer Restart remains semantically and operationally distinct.
- No steady-state VP latency, queue target, frame offset, drop rate, or CPU/GPU
  workload regression is measured.
- x64 Release and the native test suite pass.
- Live-HDMI validation passes the asymmetric, equal-queue, and repeated-reset
  matrix.

## References

- VP-0013: DirectShow queue/reset alignment and no-drop review
- VP-0043: madVR startup and handoff graph re-prime
- VP-0054: DirectShow handoff queue saturation and UI-liveness recovery
- Microsoft DirectShow push-source seeking and flushing documentation
- MPC-HC BaseSplitter and LAV Splitter reset/seek implementations
- `C:\Videoprocessor\vp\logs\vp_debug.log`
- `C:\Videoprocessor\vp\logs\vp_debug.log.0`
