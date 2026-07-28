# VP-0046: DirectShow event plumbing and passive health diagnostics

## Status

Backlog. Investigation only; no playback-policy implementation is authorized.

## User story

As a VideoProcessor maintainer, I want trustworthy, behavior-neutral
DirectShow lifecycle and downstream-pressure diagnostics, so future recovery
decisions can be evaluated from evidence without changing the stable queue,
timing, conversion, or delivery pipeline.

## Context

VP-0043 restored the narrowly proven madVR startup and handoff re-prime.
Broader automatic recovery remains risky: previous queue and timing changes
have caused dropped frames, higher latency, and worse playback.

Several possible diagnostic gaps were identified during VP-0043 review, but
none justifies changing live behavior yet.

## Investigation areas

### DirectShow graph-event plumbing

`IMediaEventEx::SetNotifyWindow` posts a notification message whose `wParam`
is zero. VP currently drains the graph event queue inside
`DirectShowVideoRenderer::OnWindowsEvent`, then the dialog interprets
`wParam` as though it were an event code.

Determine a safe way to surface the actual `GetEvent` results for logging.
The first phase must not schedule resets or change renderer state in response
to newly visible events.

Candidate events include:

- `EC_DISPLAY_CHANGED`
- `EC_VIDEO_SIZE_CHANGED`
- `EC_CLOCK_CHANGED`
- `EC_DEVICE_LOST`
- `EC_ERRORABORT`
- `EC_NEED_RESTART`
- `EC_QUALITY_CHANGE`
- `EC_STARVATION`

The investigation must establish which events madVR actually emits and their
frequency during startup, NLS/profile changes, fullscreen transitions, display
mode changes, and stable playback.

### Existing downstream-pressure evidence

Inventory and validate signals VP already measures:

- synchronous `Deliver()` call duration and slow-delivery counts;
- `GetDeliveryBuffer()` wait/failure behavior;
- raw and converted queue depth and persistence;
- delivery failures and renderer drop counters;
- renderer generation and reset history.

Do not add per-frame logging or synchronization to the hot path. Prefer
existing aggregates and low-frequency snapshots.

### Optional passive renderer interfaces

Probe `IQualProp` only as a capability/diagnostic experiment. If supported,
evaluate whether its drawn/dropped frame and synchronization statistics agree
with VP and madVR observations.

No playback behavior may depend on `IQualProp` in this story.

### Stop/run state diagnostics

Review whether the current fixed delay and `IMediaControl::Stop`/`Run` result
logging distinguish requested state from completed state. Any proposed
`GetState` confirmation must be bounded, must not block the UI or delivery
path materially, and requires evidence before implementation.

## Explicit exclusions

- Do not investigate or restore madVR `IQualityControl`; live testing proved
  the renderer returns `E_NOINTERFACE`, and the VP experiment was removed.
- Do not change queue size, targets, high-water thresholds, buffering,
  conversion, timestamps, PPM logic, cadence correction, allocator behavior,
  or delivery scheduling.
- Do not add automatic graph resets, reset escalation, cooldown policy, or
  event-triggered recovery.
- Do not change NLS/profile transition behavior.
- Do not add steady-state per-frame instrumentation.

## Required method

1. Establish a known-good baseline build and representative playback cases.
2. Add only bounded, read-only diagnostics.
3. Compare CPU use, VP queue state, conversion time, delivery latency, dropped
   frames, and visible playback against the baseline.
4. Revert any diagnostic that measurably worsens the pipeline.
5. Record which events and interfaces madVR actually provides.
6. Produce a recommendation. Any behavioral change must be a separate story
   with its own evidence, acceptance criteria, and explicit approval.

## Verification

- Initial madVR startup.
- Alpha-to-madVR and madVR-to-Alpha handoffs.
- NLS/profile changes that do and do not require renderer replacement.
- Fullscreen/windowed transitions.
- Display-mode and refresh-rate changes.
- At least one extended stable playback session.

## Acceptance criteria

- Actual DirectShow graph events can be logged with their real event code and
  parameters without reading `wParam` as the event code.
- Diagnostics are bounded and behavior-neutral.
- No new reset or queue behavior is introduced.
- No measurable regression appears in drops, queue stability, conversion
  time, delivery latency, CPU use, or picture continuity.
- Supported and unsupported passive interfaces are documented from live
  evidence.
- The final recommendation clearly distinguishes proven signals from
  speculation.
