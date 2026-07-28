# VP-0043: madVR startup and handoff graph re-prime

## Status

Done. Merged to `v1.1.014-beta` by
[PR #21](https://github.com/billslack2/videoprocessor/pull/21) on
July 28, 2026.

## User story

As a madVR/DirectShow user, I want VP to perform one delayed stop/reset/run
re-prime after initial madVR startup or a handoff into madVR, so madVR fills
its internal queues without requiring manual `R`.

## Reported evidence

During an Alpha-to-madVR switch, the OSD and madVR overlay showed a usable but
underfilled pipeline:

- VP queue: `0/1/1/32` (raw/converted/total/capacity)
- madVR decoder/upload/render queues: approximately `1-2 / 8`
- madVR present queue: `2-3 / 3`
- no reported drops; conversion average approximately 1.8-2.1 ms

The delayed recovery timer fired, but it was queue-only:

```text
Reset scheduled: reason=post-renderer-start scope=live-queue delay=3000ms
Reset executing: reason=post-renderer-start scope=live-queue
DirectShowVideoRenderer::ResetLiveQueue() - flushing live source queue only
```

Manual `R` recovered madVR through a different operation:

```text
Reset executing: reason=manual scope=graph
DirectShowVideoRenderer::Reset() - Stopping graph for complete restart
DirectShowVideoRenderer::Reset() - Graph restarted
```

## Root cause

The NLS stabilization change correctly avoided a second graph interruption
after profile-driven renderer replacement, but applied the same queue-only
scope to every newly created renderer. This unintentionally removed the
proven stop/reset/run re-prime from initial madVR startup and renderer
handoffs.

## Implemented behavior

- Initial DirectShow/madVR startup schedules one delayed graph-scoped
  stop/reset/run re-prime.
- A renderer selection or backend handoff into DirectShow/madVR schedules the
  same graph-scoped re-prime.
- NLS, shader, viewport, and other profile-only renderer replacements retain
  the existing live-queue re-prime to avoid a second visible interruption.
- Alpha retains its queue-only lifecycle behavior.
- Each pending reset records the renderer generation. A timer created for an
  older renderer is discarded rather than applied to its replacement.
- Reset logs identify renderer name, backend, generation, reason, scope, and
  delay.

This story does not change VP queue sizing, conversion, timestamp generation,
delivery scheduling, or queue-pressure recovery.

## `IQualityControl` result

Live madVR testing returned `E_NOINTERFACE` (`0x80004002`) when VP queried the
renderer for `IQualityControl`, and madVR produced no upstream quality
notifications. The experimental probe, logging, snapshot state, and unused
scene-correction gate were removed before merge.

`IQualityControl` is not a dependency or follow-up direction for madVR.

## Verification

- The rebased x64 Release build completed successfully.
- The native test suite passed: 183/183.
- Live startup logs confirmed:

```text
Reset scheduled: renderer=DirectShow - madVR backend=DirectShow
  generation=2 reason=post-renderer-start scope=graph delay=3000ms
Reset executing: renderer=DirectShow - madVR backend=DirectShow
  generation=2 reason=post-renderer-start scope=graph
DirectShowVideoRenderer::Reset() - Graph stopped
DirectShowVideoRenderer::Reset() - Graph restarted
```

## Acceptance criteria

- Initial madVR startup performs one delayed stop/reset/run re-prime.
- Handoff into madVR uses the same graph-scoped recovery.
- Profile-only NLS/shader/viewport replacement remains live-queue scoped.
- Alpha queue behavior is unchanged.
- A stale reset timer cannot affect a newer renderer generation.
- No queue, conversion, timestamp, or delivery algorithm is changed.

## Follow-up

VP-0045 owns read-only investigation of DirectShow graph-event plumbing and
passive renderer-health diagnostics. It may not change pipeline behavior
without separate evidence and approval.
