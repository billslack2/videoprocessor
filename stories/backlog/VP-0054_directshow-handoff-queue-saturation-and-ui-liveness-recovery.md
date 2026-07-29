# VP-0054: DirectShow handoff queue saturation and UI-liveness recovery

## Status

Backlog. This is an evidence-led investigation and recovery-design story. Do
not alter normal queue sizing, buffering, or reset timing merely to make the
symptom disappear.

## User story

As a VideoProcessor user switching from the Alpha renderer to DirectShow, I
need VP to remain responsive and recover a stalled handoff before capture and
renderer queues become unusable, so I do not have to quit and relaunch the
application.

## Observed incident

The deployed log location is `C:\Videoprocessor\vp\logs`. The current log
started only after the forced relaunch; the relevant evidence is in
`vp_debug.log.0` on 2026-07-28:

```text
22:01:04 Renderer shortcut render.2 selected: D
22:01:05 Renderer transition ... generation=14 ... renderer-start renderer=DirectShow - madVR
22:01:06 Reset scheduled ... generation=14 reason=post-renderer-start scope=graph delay=5000ms
22:01:06 Reset scheduled ... generation=14 reason=display-transition scope=graph delay=33000ms
22:01:07 OnVideoFrame: Raw queue BACKING UP (raw=24/32, converted=32, buffering=0, convFrames=45)
22:01:22 OSD dropped counters ... capture_missed=13 ... queue=31/32
22:01:35 Renderer transition ... renderer-stop renderer=DirectShow - madVR
```

No `Reset executing` record exists for DirectShow generation 14 before the
application became unresponsive. The VP dialog ceased repainting/responding;
Windows itself remained responsive and relaunching VP restored normal playback.
The evidence does not yet prove whether the failure is a UI-thread wait,
delivery-thread stall, reset-coordinator scheduling error, graph callback
re-entry, or a combination. It does prove that a 33-second display-transition
schedule displaced or delayed the five-second post-start re-prime while both
queues were saturating.

VP-0043 provided a narrowly scoped startup/handoff graph re-prime. This is not
a duplicate: it addresses the observed full-queue/liveness failure where that
re-prime did not execute in time and the UI stopped updating. Coordinate with
VP-0046's passive DirectShow diagnostics; do not duplicate its general
event-plumbing experiment.

## Required investigation

1. Reproduce Alpha -> DirectShow handoffs with a refresh/display resync at
   23.976 and 59.94 Hz, including renderer/profile changes that cause the
   display-transition reset path.
2. Trace the complete reset lifecycle by renderer generation: requested,
   coalesced/replaced, suppressed, scheduled deadline, started, completed,
   cancelled, and failure reason. Log the identity and priority of the request
   that replaces another request.
3. Add bounded, rate-limited liveness snapshots for the UI message loop,
   capture callback, conversion worker, delivery worker, and graph-control
   operation. Include last successful frame input, conversion, dequeue,
   downstream `Deliver`, and UI-paint/message timestamps; queue depth/capacity;
   buffering; renderer generation; and any lock/wait ownership that can be
   observed without blocking a hot path.
4. Establish whether the UI shares a blocking lock or synchronous call with
   delivery/reset/graph teardown. The diagnosis must distinguish a blocked UI
   from a healthy UI that merely has no invalidation request.
5. Compare the automatic and manual `R` reset paths. Determine exactly which
   graph, queue, timing-origin, worker, and media-control operations the manual
   recovery performs successfully.

## Recovery design requirements

Only after the investigation identifies a safe condition, implement a single
generation-aware recovery coordinator with these properties:

- A critical, persistent queue saturation plus no delivery progress may request
  the same complete graph re-prime that manual reset proves safe; queue depth
  alone is not enough because temporary fullness can be valid.
- A critical liveness recovery must not be hidden behind a long display-settle
  delay when the active DirectShow generation is otherwise ready. Define and
  log deterministic priority/coalescing rules rather than relying on the last
  timer request to win.
- Never recover an obsolete generation or one being intentionally replaced.
  Confirm the active renderer, graph state, and generation before action.
- Bound attempts with a cooldown and clear completion/failure outcome. No
  recursive reset loop, repeated teardown, or unbounded queue flushing is
  permitted.
- All waits must be bounded and must not run on the UI thread. UI status and
  controls must continue to pump/repaint during an external-renderer stall.
- Preserve successful steady-state queue, latency, timestamps, capture,
  shader/NLS, HDR, and display-transition behavior.

## Explicit exclusions

- Do not increase the 32-frame queue capacity or loosen normal high-water
  limits as a workaround.
- Do not reset merely because a queue is briefly empty, briefly full, or a
  renderer changes.
- Do not add per-frame synchronous logging, polling loops, or blocking
  `IMediaControl::GetState` calls to the capture/conversion/delivery hot path.
- Do not infer madVR state from unavailable `IQualityControl` feedback.

## Validation

Test at minimum:

| Scenario | Required result |
| --- | --- |
| Alpha -> DirectShow with 23.976 display resync | UI remains responsive; graph fills and delivers without manual intervention |
| Alpha -> DirectShow with 59.94 display resync | Same, with no sustained high-water queue |
| DirectShow -> Alpha -> DirectShow repeated | No stale generation recovery, queue growth, or UI stall |
| Display/profile/NLS change during handoff | Requests coalesce deterministically and the active generation wins |
| Simulated/observed blocked downstream delivery | Diagnostics identify the stalled stage; any enabled recovery is bounded and logged |
| Manual `R` during a pending automatic recovery | One coherent reset path, no overlapping teardown or deadlock |
| Extended stable DirectShow playback | No regression in drops, latency, queue depth, or CPU use |

## Acceptance criteria

- The next occurrence produces enough evidence to identify the blocking stage
  and why the scheduled reset did or did not execute.
- The UI cannot become permanently unresponsive because of a renderer handoff,
  queue-health check, or recovery wait.
- A proven stalled active DirectShow generation recovers through one bounded,
  manual-reset-equivalent graph re-prime or emits a clear terminal diagnostic
  if recovery cannot safely proceed.
- Normal transitions and steady playback do not gain false resets, frame drops,
  extra latency, or queue churn.

## References

- `C:\Videoprocessor\vp\logs\vp_debug.log.0` (2026-07-28 22:01:04-22:01:35)
- VP-0043: madVR startup and handoff graph re-prime
- VP-0046: DirectShow event plumbing and passive health diagnostics
- `src\VideoProcessor-Lib\CBufferedLiveSourceVideoOutputPin.*`
- `src\VideoProcessor-Lib\ALiveSourceVideoOutputPin.*`
- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`
