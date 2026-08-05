# VP-0088: Expose fast Alpha-native display refresh measurement

## Status

In progress (2026-08-05). Live validation rejected the deployed Alpha
completion-clock fallback: it reported approximately 57 Hz during a composed
swap-chain transition while madVR correctly reported 59.950622 Hz. The
corrective candidate uses a target-bound physical vblank clock for Alpha and
madVR's own runtime value for madVR. It is built and tested but **not deployed**
pending user review.

## Implementation progress

- Implemented on local feature branch `codex/vp-0088-native-refresh` at
  `3286abd`, `c587910`, `8bfc9d8`, and `53554db` from the current default
  integration branch `v1.1.015-beta`.
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
- The deployed `53554db` revision used the QPC completion timestamp after
  `pl_swapchain_swap_buffers`, with DXGI frame statistics as a preferred input
  and configured-mode normalization as a fallback. Live logs disproved that
  model: after a COMPOSED/BITBLT transition with zero frame statistics it
  estimated 57 Hz from VP pacing. The corrective candidate replaces it.
- The deployed revision also reset the display epoch at swap-chain recreation.
  That is now treated as a bug: diagnostics and correction phase must remain
  continuous across that renderer lifecycle event.
- GUI selection is now configuration override, then renderer-native madVR or
  Alpha, then truthful `Warming`. The `WaitForVBlank` worker, estimator,
  fallback, warm-up, comparison, and associated tests were removed.
- The full x64 Release solution builds successfully, including
  `VideoProcessor-GUI`, `VideoProcessor-Test`, and
  `VideoProcessorVPRenderer.dll`.
- Focused Alpha presentation telemetry and plugin-proxy tests pass 18/18,
  including periodic DXGI disjoints, 24-on-60 completion normalization,
  explicit mode-family epoch reset, and forwarding the output-mode hint across
  the Alpha plugin boundary. The full suite passes 573/574; the unrelated
  existing `ConfigurationReferenceMatchesPublicFieldInventory` test rejects
  the active `general.fullscreen=true` configuration entry.
- Deployed the clean committed `53554db` x64 Release runtime pair to
  `C:\Videoprocessor\vp` on 2026-08-05. The previous `c587910` host/plugin pair
  is backed up at `backup-before-vp0088-renderer-clock-20260805-001305`;
  deployed SHA-256 hashes match the build artifacts (`VideoProcessor.exe`
  `DA878990...A5CE`, renderer DLL `25061DC9...6E2`). Active configuration and
  state hashes were unchanged.
- Live Alpha and madVR transition validation remains pending.
- Corrective candidate (uncommitted on `codex/vp-0088-native-refresh`):
  - removes completion-timestamp and nominal-mode normalization as refresh-rate
    evidence completely; those values describe VP delivery/pacing, not the
    physical display;
  - adds a target-bound `IDXGIOutput::WaitForVBlank`/QPC clock with a rolling
    20-second rate window. It becomes valid after eight physical waits spanning
    0.25 seconds, persists across renderer/window/swap-chain/queue/scene
    transitions, and starts a new epoch only when the output monitor or active
    target refresh changes;
  - uses the physical clock's rate, phase, and synchronized seconds in Alpha's
    display telemetry and scene correction. DXGI frame statistics remain
    present-correlation diagnostics only, and a missing physical measurement
    fails closed rather than inventing a rate;
  - polls madVR `IMadVRInfo::refreshRate` on the graph owner every second,
    independently of the existing 30-second full diagnostics poll, while
    retaining the last good rate through a transient COM read failure;
  - corrects `DwmGetCompositionTimingInfo` to use `NULL` on modern Windows;
    DWM remains diagnostic/DirectShow phase data rather than an Alpha rate
    source.
- The candidate has a successful x64 Release solution build and focused timing
  tests 18/18. The full suite is 573/574; the only failure is the known,
  unrelated `ConfigurationReferenceMatchesPublicFieldInventory` assertion on
  `general.fullscreen=true`.

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
2. Publish the first finite, plausible Alpha-native rate from a target-bound
   physical vblank observation after only a short, bounded sample interval; do
   not wait for a multi-second quarantine or a 20-second averaging window. Do
   not use renderer completion timestamps, input/capture cadence, or a nominal
   mode rate as refresh-rate evidence.
3. Make renderer-native values the only automatic selected-rate source:
   madVR uses `IMadVRInfo` and Alpha uses its presentation telemetry. An
   explicit `[display_refresh_rate_override]` remains authoritative.
4. Use a target-bound `WaitForVBlank` worker as Alpha's physical source. It is
   not a generic fallback or a renderer-completion proxy; it must be keyed to
   the output monitor and can use `QueryDisplayConfig` only to detect an actual
   target mode transition.
5. Average physical vblank intervals over a rolling 20-second window after the
   short first publication. DXGI frame statistics may cross-check presentation
   correlation but cannot replace the physical rate when a composed/bitblt
   chain has no frame statistics.
6. Give display timing its own lifetime. Restart elapsed display-synchronization
   time only for an actual output-monitor or active refresh-mode change.
   Swap-chain/window initialization, queue/capture/source/scene transitions,
   `DXGI_ERROR_FRAME_STATISTICS_DISJOINT`, and temporary unavailable samples
   must not restart it.
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
2. Test disjoint statistics and counter/QPC regression as local DXGI resets
   that preserve the renderer clock, rate, and display-timing generation. Test
   that explicit swap-chain/mode-family changes withdraw the published rate,
   while queue-generation changes and transient unavailable samples preserve
   the display clock and published rate.
3. Add selection-policy tests proving precedence is override, valid
   renderer-native measurement, then `Warming` when the selected renderer has
   no valid native measurement.
4. Live validate windowed and fullscreen Alpha at 23.976 and 59.94/60 Hz,
   including an Alpha-driven refresh switch. Native measurement should become
   available from the first short coherent presentation interval, whether its
   evidence comes from DXGI statistics or normalized renderer completions (the
   exact first-report time is logged).
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
