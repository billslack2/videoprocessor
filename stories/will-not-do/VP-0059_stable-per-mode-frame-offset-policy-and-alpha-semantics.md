# VP-0059: Stable per-mode frame-offset policy and Alpha semantics

## Status

Will Not Do (superseded by VP-0069 on 2026-08-02). The frame-offset and
Alpha-semantic requirements remain required, but are now part of the single
end-to-end low-latency qualification story rather than a separate policy
project. No implementation was performed under this story.

## User story

As a VideoProcessor user, I want frame timing lead to be selected
automatically and stably for each real playback mode, so VP minimizes
avoidable scheduling latency without continuously retuning, restarting the
renderer, or sacrificing reliable presentation.

## Problem statement

`frame_offset` adds a positive amount to each DeckLink capture timestamp. It
exists because capture timestamps otherwise arrive in the past and a
timestamp-scheduled renderer can render immediately instead of at a stable
planned time. On the DirectShow/external-renderer path, that positive lead can
therefore trade latency for fewer late samples and less jitter.

The current AUTO implementation is not a live-latency measurement. Every five
seconds it derives an offset only from configured queue size and input refresh:

```text
nominalTarget = queue_size / 8
frames = nominalTarget + 1 at >30 Hz
offset = rounded(frames * frameDuration)
```

If the derived result differs by at least 2 ms, it changes the capture
timestamp offset and requests a renderer reset. For example, a 32-frame queue
at 59.94 Hz derives roughly 85 ms, while an Alpha queue of 1-3 derives roughly
20 ms. This is a heuristic, not evidence of end-to-end video latency, and
periodic adjustment is inappropriate for a control that can rebuild the
renderer.

Alpha uses the timestamp for frame-rate/PPM observation and entry-latency
telemetry, but currently queues frames immediately in its own render thread;
it does not schedule an individual present for the future timestamp. A
constant positive frame offset can thus make Alpha's `VP Lat` display more
negative without necessarily adding equivalent physical presentation delay.
This must be proven for all Alpha timing/cadence consumers before changing
behavior.

The goal is to remove avoidable latency—e.g. move a stable DirectShow path
toward 200 ms rather than 300 ms—not to claim that timestamp lead can remove
capture, source, GPU, vsync, or projector/display processing latency.

## Required design decisions

1. **Separate semantics by backend.** Establish and document whether frame
   offset is a required scheduling lead for DirectShow and a diagnostic-only
   timestamp shift for Alpha. If Alpha does not require it for timing or
   cadence, default Alpha to zero or explicitly ignore the DirectShow offset;
   do not silently let a DirectShow setting distort Alpha latency telemetry.
2. **Automatic mode selection.** Replace periodic retuning with a bounded
   selection when a stable capture/display mode or renderer backend begins.
   Re-evaluate only after a meaningful contract change: capture device/input,
   raster, nominal refresh family, timing method, or renderer backend.
3. **Persistent per-mode result.** Cache an automatic result by at least
   capture device/input, raster/nominal refresh, timing method, and renderer
   backend. Define whether the cache belongs in a separate runtime state file
   or a clearly documented configuration section. It must not overwrite a
   developer's explicit manual setting.
4. **Manual escape hatch.** Retain an advanced explicit override and an
   explicit `AUTO`/default selection. A manual offset has legitimate uses for
   unusual card/driver/display paths and A/V synchronization investigation.
5. **Truthful UI and diagnostics.** Show whether the active value is manual,
   cached automatic, newly measured automatic, or unused by the selected
   backend. Do not label a queue-size heuristic as measured end-to-end latency.

## Measurement and safety requirements

- Inventory every consumer of `VideoFrame::GetTimingTimestamp()` for both
  backends, including DirectShow sample scheduling, Alpha PPM/cadence logic,
  OSD latency, scene-aware timing, and reset/re-prime behavior.
- Define a DirectShow lead measurement from signals VP can actually observe:
  capture arrival versus hardware timestamp, delivery buffer waits/failures,
  `Deliver()` duration/failure, queue progression, and first successful
  downstream acceptance. madVR's unavailable `IQualityControl` must not be
  assumed as a source of feedback.
- Determine the smallest stable positive lead from a bounded startup/mode
  calibration window. Use percentile/jitter margin and a hard minimum/maximum,
  not a single instantaneous sample.
- Calibration and cache application must run off the capture/delivery/UI hot
  paths. It must never update every few seconds during stable playback, create
  a renderer-reset loop, or change a live value merely because queue depth
  fluctuates.
- A calibration result must be accepted only after stable evidence. On missing
  evidence, use a conservative backend-specific fallback and log why; never
  guess a new lower value.
- Changes that require a DirectShow re-prime must coalesce through the existing
  reset coordinator and cannot run during renderer replacement, display resync,
  queue-liveness recovery, or another pending reset.

## Investigation matrix

| Backend / mode | Required finding |
| --- | --- |
| DirectShow at 23.976/24 | Minimum stable scheduling lead; effects on late delivery, drops, and latency |
| DirectShow at 50 and 59.94/60 | Same; results must not be copied blindly from 24 Hz |
| Alpha at 23.976 and 59.94 | Whether offset changes physical present time, cadence, PPM, or only telemetry |
| Alpha queue depth 1 and 3/4 | Confirm queue depth and offset are independent controls |
| Renderer switch and HDMI/display resync | Cache key selection, safe application timing, and no reset loop |
| Explicit manual offset | Exact precedence and reversible return to AUTO |
| Extended stable playback | No periodic offset changes, unexpected reset, queue instability, or drop regression |

Use an external end-to-end latency measurement where available (camera/LED or
known capture/display test) to distinguish VP scheduling savings from source,
DeckLink, GPU, and display/projector latency. VP cannot infer display light
output solely from its internal clocks.

## Explicit exclusions

- Do not promise one-frame or fixed end-to-end latency.
- Do not remove the manual control until automatic behavior is validated across
  hardware and the documented override remains unnecessary in practice.
- Do not use `frame_offset` to hide unrelated Alpha queue, source, capture,
  or projector latency.
- Do not modify audio-delay policy as part of this work.
- Do not continuously tune a live offset in steady playback.

## Acceptance criteria

- DirectShow gets one stable, evidence-backed lead per relevant mode, with no
  steady-state periodic adjustment.
- Alpha's treatment is proven and made explicit: either it safely ignores the
  scheduling lead/defaults to zero, or a documented Alpha-specific reason
  requires a stable value.
- Manual configuration takes priority and is clearly visible; automatic cache
  results never overwrite it.
- Logs and UI describe the source, key, evidence, and application decision for
  the active offset.
- Measured latency does not regress, while valid paths can reduce unnecessary
  scheduled lead without late delivery, dropped frames, queue starvation,
  renderer-reset loops, or A/V sync regression.

## References

- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`:
  `CalculateAutoFrameOffset`, five-second AUTO update, and reset request
- `src\VideoProcessor-Lib\ACaptureDevice.h`: timestamp-lead contract
- `src\VideoProcessor-Lib\blackmagic_decklink\BlackMagicDeckLinkCaptureDevice.cpp`:
  timestamp offset application
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`:
  Alpha immediate queueing and timestamp telemetry
- VP-0046: DirectShow passive health diagnostics
- VP-0054: DirectShow handoff queue saturation and UI-liveness recovery
