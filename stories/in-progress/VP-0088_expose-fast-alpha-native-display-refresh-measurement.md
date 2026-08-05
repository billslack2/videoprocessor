# VP-0088: Expose fast Alpha-native display refresh measurement

## Status

In progress (2026-08-05). The current Alpha renderer has a fast, renderer-native
measurement path, but it is not surfaced through `IRenderer` or used by the
GUI/OSD. This story wires it through safely, makes the renderer-native paths
authoritative, and removes the legacy generic display sampler.

## Implementation progress

- Implemented on local feature branch `codex/vp-0088-native-refresh` at
  `3286abd` and `c587910` from the current default integration branch
  `v1.1.015-beta`.
- Alpha now publishes a finite, plausible renderer-native rate after its second
  coherent current-generation frame-statistics sample. The later eight-sample,
  0.25-second `Stable` evidence gate remains separate for phase-sensitive Alpha
  cadence work.
- The Alpha plugin proxy forwards `GetDetectedDisplayRefreshRate`. The
  renderer-native value is averaged over a rolling 20-second window after its
  initial two-sample publication.
- Alpha now separates the display-timing epoch from queue, source, and scene
  detector generations. Its correction phase integrates capture/display drift
  using seconds since display synchronization; queue resets, backlog recovery,
  source changes, and detector changes can invalidate transient action
  ownership but do not restart that clock or withdraw the native rate.
- A real display-timing discontinuity (including disjoint/regressing DXGI
  statistics or a swap-chain/display reinitialization) withdraws the native
  rate and starts a new display-timing epoch. Diagnostics expose
  `display_sync_s` so continuity is live-verifiable.
- GUI selection is now configuration override, then renderer-native madVR or
  Alpha, then truthful `Warming`. The `WaitForVBlank` worker, estimator,
  fallback, warm-up, comparison, and associated tests were removed.
- The full x64 Release solution builds successfully, including
  `VideoProcessor-GUI`, `VideoProcessor-Test`, and
  `VideoProcessorVPRenderer.dll`.
- Focused Alpha telemetry/cadence, native selection, output-readiness, and
  plugin-proxy tests pass 59/59. The full suite passes 568/569; the unrelated
  existing `ConfigurationReferenceMatchesPublicFieldInventory` test rejects
  the active `general.fullscreen=true` configuration entry.
- Live Alpha and madVR transition validation remains pending; no deployment was
  performed.

## User story

As an Alpha renderer user, I want VP to display and use the actual output
refresh rate as promptly and accurately as the madVR renderer does, so refresh
switches do not spend an unnecessary multi-second period in `Warming` and
timing diagnostics describe the renderer's real presentation cadence.

## Current behavior and evidence

madVR reports its own settled rate through `IMadVRInfo`; VP exposes that through
`DirectShowVideoRenderer::GetDetectedDisplayRefreshRate`.

Alpha currently does not override `IRenderer::GetDetectedDisplayRefreshRate`.
The Alpha render loop already has a more direct source: after present it calls
`IDXGISwapChain::GetFrameStatistics` and feeds `SyncRefreshCount` plus
`SyncQPCTime` into `AlphaPresentationTelemetry`. That telemetry calculates
`measuredDisplayHz` from actual presentation refresh counts after at least eight
samples spanning at least 0.25 seconds. It is presently used only by Alpha's
cadence-correction/presentation-target logic and periodic debug logging.

The legacy GUI `DisplayRefreshRateSampler` measures monitor vblank with
`IDXGIOutput::WaitForVBlank` and QPC, but its multi-second quarantine and
evidence policy makes it unsuitable for current VP use. With renderer-native
madVR and Alpha measurements available, it provides no selected-rate, fallback,
warm-up, or comparison value and must be removed.

## Scope

1. Define an Alpha renderer-native refresh-rate reporting contract through
   `IRenderer::GetDetectedDisplayRefreshRate`, backed by the current
   presentation telemetry.
2. Publish the first finite, plausible Alpha-native rate as soon as two coherent
   current-generation frame-statistics samples permit a rate calculation; do
   not wait for the legacy sampler, an arbitrary multi-second quarantine, or
   the later `Stable` evidence threshold. Reject disjoint or regressing DXGI
   statistics and implausible rates.
3. Make renderer-native values the only automatic selected-rate source:
   madVR uses `IMadVRInfo` and Alpha uses its presentation telemetry. An
   explicit `[display_refresh_rate_override]` remains authoritative.
4. Remove `DisplayRefreshRateSampler` and its `WaitForVBlank` worker, state,
   warm-up/quarantine policy, OSD inputs, and selection logic. Do not retain it
   as a fallback, long-run phase reference, or diagnostic comparison. Do not
   replace it with `QueryDisplayConfig`; that is only a configured target-rate
   guardrail, not a physical timing measurement.
5. Average coherent Alpha-native display cadence over a rolling 20-second
   window after the immediate two-sample publication. Validate it only against
   its display-timing generation and the configured target refresh family.
   Quarantine and log an implausible value rather than feeding it into timing,
   PPM, or OSD state.
6. Give display timing its own lifetime. Clear the Alpha-native value and
   restart elapsed display-synchronization time only for an actual display
   timing discontinuity: swap-chain/display initialization, output-monitor or
   refresh transition, regressing/incoherent statistics, or
   `DXGI_ERROR_FRAME_STATISTICS_DISJOINT`. A temporary unavailable sample must
   not discard an otherwise coherent clock.
7. Queue reset, backlog recovery, capture/source generation, scene-detector
   generation, scene events, and UI/OSD activity must not restart the display
   synchronization clock, rate window, or accumulated cadence phase. They may
   cancel or detach unsafe pending action/verification ownership.
8. Improve diagnostics without confusing the values: log source, generation,
   evidence state, sample count, native rate, configured target rate, and the
   reason whenever the selected source changes. The OSD must show `Warming`
   only until a valid renderer-native rate exists and identify a
   quarantined/unavailable result truthfully.

## Non-goals

- Do not use the capture/input frame rate as the display rate.
- Do not trust a nominal Windows display-path rate as the actual measured rate.
- Do not derive cadence elapsed time from queue, capture, source, scene, or OSD
  lifetimes.

## Validation

1. Add focused unit tests for `AlphaPresentationTelemetry` covering 23.976,
   24, 50, 59.94, and 60 Hz synthetic frame-statistics sequences; require a
   current, plausible published rate as soon as two coherent samples exist,
   while separately retaining tests for the later stable-evidence state.
2. Test disjoint statistics, counter/QPC regression, display-timing generation
   changes, and implausible rates; each must withdraw the published rate. Test
   that queue-generation changes and transient unavailable samples preserve the
   display clock and published rate.
3. Add selection-policy tests proving precedence is override, valid
   renderer-native measurement, then `Warming` when the selected renderer has
   no valid native measurement.
4. Live validate windowed and fullscreen Alpha at 23.976 and 59.94/60 Hz,
   including an Alpha-driven refresh switch. After the first valid present,
   native measurement should normally become available immediately after the
   second coherent frame-statistics sample (the exact first-report time is
   logged).
5. Confirm that the displayed rate clears during a mode change rather than
   showing a stale old rate, and that cadence correction, queue stability,
   latency, OSD, and SDR/HDR behavior do not regress.
6. Confirm through `display_sync_s` and phase diagnostics that live queue reset,
   backlog recovery, capture/source replacement, and scene-detector reset do
   not restart display elapsed time or accumulated drop/repeat timing.

## Relevant code

- `src\\VideoProcessor-GUI\\VideoProcessorDlg.cpp`:
  selected-rate precedence, legacy sampler removal, and OSD input.
- `src\\VideoProcessor-Lib\\vprenderer\\AlphaPresentationTelemetry.*`:
  existing swap-chain frame-statistics cadence measurement.
- `src\\VideoProcessor-Lib\\vprenderer\\LibplaceboVideoRenderer.cpp`:
  `IDXGISwapChain::GetFrameStatistics` collection and Alpha render loop.
- `src\\VideoProcessor-Lib\\IRenderer.h`:
  renderer-native refresh reporting contract.
- `src\\VideoProcessor-Lib\\microsoft_directshow\\video_renderers\\DirectShowVideoRenderer.*`:
  existing madVR-native implementation to mirror semantically.
