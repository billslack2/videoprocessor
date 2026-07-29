# VP-0063: Automatic Alpha-to-madVR handoff re-prime

## Status

Backlog. This is a narrow renderer-handoff policy and diagnostics story.

## User story

As a VideoProcessor user switching from the Alpha renderer to madVR, I want
VP to automatically reach the same clean, low-latency DirectShow state that a
manual Reset achieves, so a handoff does not leave the internal queue elevated
or require pressing `R`.

## Reported behavior

During an Alpha -> madVR renderer switch, the VP internal queue rises somewhat
above its normal steady state and apparent video latency increases. The user
does not see an automatic reset/re-prime occur. Pressing manual `R` returns
latency and queue behavior to normal.

VP knows the source and destination renderer backends and assigns renderer
generations during this transition. The issue is therefore not discovering
that a handoff occurred; it is determining whether the reset coordinator
requests, suppresses, coalesces, executes, and completes the required
DirectShow re-prime for the *new active generation*.

## Relationship to existing work

- VP-0043 introduced a delayed DirectShow startup/handoff re-prime.
- VP-0054 added DirectShow handoff queue saturation/liveness recovery.
- VP-0061 defines a correct manual in-place DirectShow flush/re-prime,
  especially for asymmetric madVR queues.

This story does not duplicate VP-0061's DirectShow flush implementation. It
uses the proven manual-reset transaction as the required automatic recovery
operation after an Alpha -> madVR handoff, once that transaction is accepted.
It must not turn every queue fluctuation into a reset.

## Required investigation

1. Reproduce Alpha -> madVR switching at 23.976/24 and 59.94/60, with and
   without a display refresh/resync. Capture a log from
   `C:\Videoprocessor\vp\logs\vp_debug.log` or a numbered rotation.
2. Trace the handoff generation from Alpha stop through madVR first-live-frame
   reveal. Log every reset request and its reason, delay, priority,
   coalescing/replacement/suppression decision, execution, and completion.
3. Compare the automatic handoff path with pressing `R` after the handoff:
   graph state, flush/new-segment epoch, raw/converted queue depth, delivery
   progress, latency, renderer generation, and elapsed time to steady state.
4. Determine whether the automatic request is absent, superseded by a
   display-transition delay, rejected because the renderer is still replacing,
   bound to an obsolete generation, or executed but incomplete.
5. Confirm whether the observed queue elevation is VP raw/converted buffering,
   DirectShow delivery blockage, expected madVR preroll, or a timing/offset
   artifact. Do not infer it solely from one OSD sample.

## Required behavior

1. When Alpha hands off to a newly active madVR/DirectShow generation, VP must
   schedule one bounded automatic re-prime once the renderer is ready for it.
   It must use the same serialized, manual-reset-equivalent DirectShow
   transaction proven by VP-0061.
2. The request must be generation-aware. Never reset a retiring Alpha instance,
   an obsolete DirectShow graph, or a graph being intentionally replaced.
3. A display transition/resync may defer re-prime until the new generation is
   safe, but it must not indefinitely mask the handoff request. Define explicit
   priority and coalescing rules; log the final deadline and winner.
4. Do not reset from queue depth alone. The automatic action is authorized by
   the known Alpha -> madVR handoff plus readiness/liveness evidence, with
   queue and delivery progress used only as confirmation/escalation signals.
5. Run no more than one automatic handoff re-prime per active generation,
   unless a separately logged, bounded failure policy authorizes a retry.
6. Preserve manual `R` as an immediate explicit operation. If it arrives while
   an automatic re-prime is pending, coalesce safely rather than overlapping
   flushes or restarts.
7. Do not add steady-state queue depth, frame offset, startup delay, renderer
   rebuild, drop burst, or UI blocking. Alpha -> Alpha, madVR -> Alpha, and
   stable madVR playback must not gain this DirectShow-only action.

## Diagnostics

For each relevant handoff, log at lifecycle granularity:

- prior and new backend/name, old/new renderer generation, display mode, and
  first-live-frame evidence;
- reset request source (`alpha-to-madvr-handoff`, display transition, manual,
  liveness, etc.), requested/executed generation, delay, deadline, priority,
  and coalescing/suppression reason;
- queue and delivery snapshot before re-prime, after flush, first discontinuous
  current-epoch sample, and steady state; and
- latency/queue comparison against an immediately subsequent manual `R` test
  when diagnosing.

Do not add per-frame logging to capture, conversion, or delivery paths.

## Validation matrix

| Scenario | Required result |
| --- | --- |
| Alpha -> madVR, same refresh | One automatic re-prime reaches manual-reset-equivalent queue and latency |
| Alpha -> madVR with refresh/display resync | Re-prime waits only for the active stable generation, then completes once |
| Repeated Alpha <-> madVR switching | No obsolete-generation reset, loop, queue growth, or UI stall |
| Manual `R` immediately after handoff | Coalesced/safe behavior; no overlapping flush transaction |
| madVR 8/8 and asymmetric queues | VP-0061 correctness remains intact; no delivery block regression |
| madVR -> Alpha and Alpha -> Alpha | No inappropriate DirectShow re-prime |
| Stable madVR session | No new automatic reset or latency/queue regression |

## Acceptance criteria

- Logs conclusively explain whether each Alpha -> madVR handoff requested and
  completed a re-prime for the active generation.
- A normal Alpha -> madVR switch reaches the same low-latency VP/DirectShow
  queue state as a manual reset without user action.
- Display resync and reset arbitration cannot silently discard the required
  active-generation handoff re-prime.
- No reset loop, renderer restart, UI hang, queue starvation, dropped-frame
  burst, or steady-state latency regression is introduced.

## References

- VP-0043: madVR startup and handoff graph re-prime
- VP-0054: DirectShow handoff queue saturation and UI-liveness recovery
- VP-0061: DirectShow in-place reset re-prime with asymmetric madVR queues
- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`: renderer transitions and
  reset-request coordination
- `src\VideoProcessor-Lib\microsoft_directshow\live_source_filter\CBufferedLiveSourceVideoOutputPin.*`
