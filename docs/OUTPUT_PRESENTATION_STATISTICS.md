# Output presentation statistics

The OSD distinguishes output observations from upstream counters:

| OSD row | Meaning |
| --- | --- |
| Capture/queue | Captured-input and renderer-queue drop counters. These are not scanout counts. |
| Display D/R | Complete displayed drop/repeat totals. Currently unavailable: the existing polling cannot establish the fate of every submitted present. |
| Display holds | Directly observed long-hold events and excess refreshes beyond a conservative intended interval. This is a partial lower bound, not a total glitch or repeated-frame count. |
| Display evidence | Verified adjacent intervals, observation gaps and current availability. |
| Cadence drift | Predicted time until a cadence correction, not a measured fault count. |
| Intentional D/R | Deliberate source cadence correction actions. |

The observer uses the existing DXGI statistics query; it adds no GPU query, wait, flush or scheduling change. It correlates only exact present IDs. A skipped ID, repeated poll, failed/unavailable statistics query or context change cannot fabricate a displayed drop or repeat. Successful render/submission alone does not prove display: libplacebo's swap-buffers call does not return Present's HRESULT to this host.

Natural 24-to-60 presentation allows up to three refreshes per source. Native 59.94-to-60 conservatively allows two; some one-refresh delays therefore cannot be distinguished from natural cadence and are not counted. Smooth Motion schedules an output for each refresh, so its allowance is one. Its inserted outputs and subtitle packet reuse are not themselves glitches. Intentional source drop/repeat correction boundaries are excluded. The observer does not assign blame for a verified long hold: source delivery, CPU, GPU, presentation or compositor delays can all contribute.

A new renderer instance starts a new statistics session. Queue recovery, resize, statistics loss and continuity resets preserve recorded incidents. Current unavailable/stale coverage never appears as a measured clean zero. Observation totals are held in a bounded fixed array and published through a small snapshot; the producer never waits for the OSD lock.

The existing AlphaPresentationTelemetry retirement frontier remains unchanged. Its historical marking of all IDs up to the latest observed count is not used as proof of per-frame display.

Windows can provide these statistics for flip-model or fullscreen presentation; windowed BitBlt may provide zeros or report unsupported. This change does not switch presentation model to obtain telemetry. Exact madVR-style complete drop/repeat and planned-deadline glitch accounting remains a separate requirement: it needs immutable scheduled presentation slots and sufficiently complete per-present display events, with validated coverage on the active output path.

References: [DXGI frame statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics), [Microsoft flip-model timing and glitch detection](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model).

Regression coverage: AlphaOutputPresentationTelemetryTests, OutputPresentationPublicationTests and StatsOverlayLayoutTests exercise cadence, delayed scanout, stale polls, gaps, unsupported/disjoint results, duplicate submissions, resets, counter wrap, record eviction, publication freshness and OSD distinction between unavailable and measured zero. These synthetic observations do not establish physical display accuracy or subtitle playback acceptance.
