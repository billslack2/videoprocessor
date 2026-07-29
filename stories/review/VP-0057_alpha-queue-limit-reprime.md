# VP-0057: Re-prime Alpha when it exceeds the configured queue limit

## Status

Review — deployed July 29, 2026.

The Alpha renderer intentionally maintains internal burst headroom: with the
user-configured queue size set to `3`, it permits an internal hard capacity of
`6`. This produced visible states such as `5/3`, even though the UI presents
the configured value as the queue limit.

The deployed debug log confirmed that manual reset was received and completed:
each request armed a new Alpha queue generation, flushed renderer state, and
then correctly re-primed to the configured depth. It did not leave the queue
empty because live capture immediately refills it.

## Implemented change

When **Auto** is enabled and the Alpha raw or converted queue becomes greater
than the configured UI limit, VP now requests an immediate queue-only reset
using the existing `QueuePressure` reason. This clears/re-primes only Alpha's
renderer-owned queue; it does not rebuild the renderer graph and does not
alter madVR/DirectShow recovery behavior.

The change is in:

- `src/VideoProcessor-GUI/VideoProcessorDlg.cpp`

## Build and deployment evidence

- Built successfully from `C:\Users\bslac\vp\videoprocessor-v1.1.015-beta`
  using x64 Release on July 29, 2026.
- Deployed only the rebuilt `VideoProcessor.exe` to `C:\Videoprocessor\vp`.
- Active configuration and all renderer runtime files were preserved.
- Rollback executable:
  `C:\Videoprocessor\vp\VideoProcessor.exe.pre-alpha-queue-limit-20260729-003750`.

## User story

As an Alpha-renderer user, I want Auto queue recovery to re-prime Alpha when
its reported queue depth exceeds the queue size I configured, so transient
burst headroom cannot become accumulated live-video latency.

## Acceptance criteria

- With Alpha selected, queue size `3`, and Auto enabled, an observed `4/3` or
  greater queue state logs `Alpha queue exceeded configured limit` and queues
  a queue-only reset.
- The reset log records `reason=queue-pressure scope=live-queue`, then Alpha
  reports a new queue generation and re-primes normally.
- No full renderer graph rebuild is performed for this recovery.
- madVR behavior is unchanged: its DirectShow liveness criteria remain the
  only automatic recovery trigger.
- With Auto disabled, queue depth alone does not trigger this new recovery.

## Review requested

Perform a short live Alpha test with queue size `3` and Auto enabled. Induce a
brief renderer stall or rapid setting changes, then confirm the current or
rotated `C:\Videoprocessor\vp\logs\vp_debug.log*` files contain the new
queue-limit message followed by a live-queue reset and normal playback.
