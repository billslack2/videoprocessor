# Output presentation statistics

The OSD separates input losses, intentional cadence corrections and observed
output incidents. Display observations are partial evidence, not a guarantee
that every physical refresh has been measured.

| OSD row | Meaning |
| --- | --- |
| Capture/queue | Captured-input and renderer-queue losses. |
| Display D/R | Matched OS-discarded presents, excluding intentional corrections; repeat value is a conservative lower bound from excessive display holds. |
| Display glitches | Observed excessive-hold incidents, not a complete count of missed presentation deadlines. |
| Display evidence | Matched outcomes and coverage gaps; partial, stale or unavailable status. |
| Cadence drift | Predicted time until cadence correction, not a measured fault. |
| Intentional D/R | Deliberate source cadence actions. |

## Passive collector and render-path contract

`VideoProcessorDisplayObserver.exe` uses the MIT PresentMon v2.1.0 PresentData
consumer, pinned to `de71bba2fb65482f9cf6486284c9eed6aa87698f`. The minimal
vendored subset, license, file hashes and local overflow instrumentation are
in `3rdparty/presentmon`. Each renderer loads its adjacent helper by absolute
path. Both renderer build/package folders carry the helper and MIT notice.

The helper observes Windows presentation events in its own lower-priority
process. GPU-work, input and frame-type tracking are disabled. There is no
service, privilege change, automatic elevation, presentation-mode switch or
change to Present flags, pacing, subtitles, AR or HDR. If tracing is denied,
the OSD reports unavailable. Collection has nonzero CPU/system overhead and
requires measured qualification; process isolation does not prove zero cost.

The renderer freezes output identity, intended PTS, context epochs and the
QPC interval containing the actual Present call. Its only handoff is a fixed
POD copy to a bounded, allocation-free, lock-free-on-x64 SPSC queue. A full
queue rejects the observation immediately. No pipe I/O, collector lock,
waiting, GPU query, flush or event matching runs on the render thread.

A background worker matches exact PID, render thread, swapchain and Present
QPC interval. It rejects ambiguous, lost, malformed and unknown evidence;
missing events are gaps, never fabricated drops. Bounded reorder storage and
elapsed-time expiry cannot hold up rendering. A small snapshot uses try-lock
publication/reading. Helper failure preserves prior incidents and makes
coverage unavailable. Normal stop, child failure and parent exit have owned
session cleanup; a simultaneous abrupt termination of parent and helper can
still leave an ETW session requiring explicit cleanup.

Raw matched discarded outcomes remain available in the snapshot separately
from unexpected drops. A rendering failure never receives an intentional
correction exemption. No-Present slots break continuity without becoming OS
discarded frames. Context changes, reset, mode changes and observation loss
require a new adjacent interval before hold inference resumes.

## Cadence and accuracy limits

Without a scheduler-owned absolute vblank target, repeats/glitches are
conservative excessive-hold estimates. Native 24-to-60 allows three refreshes
per source interval; 59.94-to-60 conservatively allows two. Smooth Motion uses
its output PTS, so inserted frames and subtitle reuse are ordinary outputs.
Intentional corrections are excluded from hold inference. A single verified
long hold counts once and reanchors at its observed end; it does not cascade.

The collector obtains events on the current composed-copy path, where DXGI
GetFrameStatistics is unsupported. It still does not establish complete
physical scanout or exact missed-deadline counts. VRR, composed presentation,
uncertain refresh rate, timestamp quantization and missing events limit what
can be inferred. Holds do not assign blame to capture, CPU, GPU or compositor.
The existing DXGI observer remains a fallback when the passive observer is
disabled. Its retirement frontier is not used as per-present display proof.

For controlled A/B qualification, launch a child with
`VP_DISPLAY_OBSERVER=0` to disable collection; unset it to enable collection.
This is a process-only diagnostic switch and does not edit user configuration.
Unit tests set it to zero unless explicitly exercising collector lifecycle.
The native counter/protocol/OSD tests cover normal and Smooth Motion cadence,
reorder, duplicates, wrong identities, queue overflow, stalls, resets, lost
health and malformed messages. Synthetic tests and a passive probe are not
physical-display or subtitle playback acceptance.

References: [PresentMon v2.1.0](https://github.com/GameTechDev/PresentMon/tree/v2.1.0),
[DXGI statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics),
[Microsoft flip-model timing](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model).

## Repeating validation

After a matching x64 Release solution build, run
`powershell -NoProfile -File tools/test_display_statistics.ps1` for the CPU,
protocol, reset and OSD regressions. Add `-IncludeCollector` to also start a
real observer for the test process, force only that child to fail, verify
cleanup, and reset counters afterward. It draws no playback window and does
not change the installed player or configuration. ETW permission failure is a
test failure, not an automatic elevation request. Use a normal user token for
qualification. The runner restores its diagnostic environment variables.

## Resetting counters

Press **Ctrl+Shift+R** to reset playback counters. In **Shortcuts**, the action is
**Reset playback counters** (`[shortcuts] reset_playback_counters`); it can be
remapped or cleared to disable the shortcut. No configuration edit is required
for the default binding.

The same reset runs when the current capture input returns from a confirmed
locked-to-unlocked-to-locked transition, or reports a changed complete native
input format. Initial startup, unknown lock observations, HDR metadata changes,
profile changes and ordinary queue/renderer recovery do not nominate a reset.
Notifications from old devices or capture runs are ignored.

Reset clears the displayed capture/queue losses, intentional drop/repeat totals,
and display hold/evidence history. It does not flush playback, change cadence
policy, or reset subtitles, HDR processing or performance averages. The manual
reset also works with the OSD hidden. Raw producer counters remain intact for
recovery logic and logs.

Display observations reset asynchronously on the render thread. The OSD shows
`reset pending` until the renderer acknowledges it; delayed pre-reset reports
cannot restore cleared incidents. Unsupported or unmeasured output statistics
remain unavailable, rather than appearing as measured zero. Fresh verified
intervals are needed before an observed zero can be shown.
