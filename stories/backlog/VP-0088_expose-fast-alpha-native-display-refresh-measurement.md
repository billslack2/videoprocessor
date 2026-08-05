# VP-0088: Expose fast Alpha-native display refresh measurement

## Status

Backlog (2026-08-04). The current Alpha renderer has a fast, renderer-native
measurement path, but it is not surfaced through `IRenderer` or used by the
GUI/OSD. This story wires it through safely and validates it against the
existing independent DXGI measurement.

## User story

As an Alpha renderer user, I want VP to display and use the actual output
refresh rate as promptly and accurately as the madVR renderer does, so refresh
switches do not spend an unnecessary multi-second period in `Warming` and
timing diagnostics describe the renderer's real presentation cadence.

## Current behavior and evidence

madVR reports its own settled rate through `IMadVRInfo`; VP exposes that through
`DirectShowVideoRenderer::GetDetectedDisplayRefreshRate`, and the GUI prefers
it over the generic measurement for a DirectShow/madVR graph.

Alpha currently does not override `IRenderer::GetDetectedDisplayRefreshRate`.
The GUI therefore uses the shared `DisplayRefreshRateSampler`, which measures
the active monitor with `IDXGIOutput::WaitForVBlank` and QPC. That remains a
valuable independent physical-vblank source, but its policy deliberately uses:

- a five-second post-transition quarantine;
- two seconds of startup evidence;
- ten seconds of readiness evidence; and
- thirty seconds before phase-sensitive confidence.

The Alpha render loop already has a more direct source: after present it calls
`IDXGISwapChain::GetFrameStatistics` and feeds `SyncRefreshCount` plus
`SyncQPCTime` into `AlphaPresentationTelemetry`. That telemetry calculates
`measuredDisplayHz` from actual presentation refresh counts after at least eight
samples spanning at least 0.25 seconds. It is presently used only by Alpha's
cadence-correction/presentation-target logic and periodic debug logging.

## Scope

1. Define an Alpha renderer-native refresh-rate reporting contract through
   `IRenderer::GetDetectedDisplayRefreshRate`, backed by the current
   presentation telemetry.
2. Publish a rate only when its telemetry generation is current, evidence is
   `Stable`, the value is finite and plausible, and no DXGI frame-statistics
   disjoint condition is active.
3. Make the GUI prefer that Alpha-native rate exactly as it already prefers the
   madVR-native rate. An explicit `[display_refresh_rate_override]` remains
   authoritative.
4. Keep the common `WaitForVBlank` sampler alive as the independent fallback,
   warm-up source, long-run phase reference, and diagnostic comparison. Do not
   replace it with `QueryDisplayConfig`; the latter is only the configured
   target-rate guardrail, not a physical timing measurement.
5. Validate native measurements against the active target refresh family and
   the fallback sampler once both are available. Quarantine and log a material
   disagreement rather than feeding an implausible native value into timing,
   PPM, or OSD state.
6. Clear the Alpha-native published value on renderer restart, swap-chain
   rebuild, output-monitor change, refresh-family transition, telemetry
   generation change, `DXGI_ERROR_FRAME_STATISTICS_DISJOINT`, and loss of frame
   statistics. A prior-generation value must never survive a transition.
7. Improve diagnostics without confusing the values: log source, generation,
   evidence state, sample count, native rate, configured target rate, fallback
   `WaitForVBlank` rate, agreement/mismatch in ppm, and the reason whenever the
   selected source changes. The OSD must continue to show `Warming` until a
   valid selected rate exists and identify a quarantined/unavailable result
   truthfully.

## Non-goals

- Do not use the capture/input frame rate as the display rate.
- Do not shorten the shared sampler's evidence requirements globally; they
  protect DirectShow and long-term phase/cadence logic.
- Do not trust a nominal Windows display-path rate as the actual measured rate.
- Do not change refresh switching, queue policy, PPM configuration, or cadence
  correction decisions beyond using a validated Alpha-native measurement where
  the current code uses the selected display rate.

## Validation

1. Add focused unit tests for `AlphaPresentationTelemetry` covering 23.976,
   24, 50, 59.94, and 60 Hz synthetic frame-statistics sequences; require
   stable output only after both the eight-sample and 0.25-second thresholds.
2. Test disjoint, unavailable statistics, counter regression, generation
   changes, and implausible rates; each must withdraw the published rate.
3. Add selection-policy tests proving precedence is override, valid
   renderer-native measurement, then validated `WaitForVBlank` fallback.
4. Live validate windowed and fullscreen Alpha at 23.976 and 59.94/60 Hz,
   including an Alpha-driven refresh switch. After the first valid present,
   native measurement should normally become available within one second (the
   exact first-report time is logged); it must agree with sustained DXGI
   `WaitForVBlank` evidence within the chosen documented tolerance.
5. Confirm that the displayed rate clears during a mode change rather than
   showing a stale old rate, and that cadence correction, queue stability,
   latency, OSD, and SDR/HDR behavior do not regress.

## Relevant code

- `src\\VideoProcessor-GUI\\VideoProcessorDlg.cpp`:
  `DisplayRefreshRateSampler`, selected-rate precedence, and OSD input.
- `src\\VideoProcessor-Lib\\vprenderer\\AlphaPresentationTelemetry.*`:
  existing swap-chain frame-statistics cadence measurement.
- `src\\VideoProcessor-Lib\\vprenderer\\LibplaceboVideoRenderer.cpp`:
  `IDXGISwapChain::GetFrameStatistics` collection and Alpha render loop.
- `src\\VideoProcessor-Lib\\IRenderer.h`:
  renderer-native refresh reporting contract.
- `src\\VideoProcessor-Lib\\microsoft_directshow\\video_renderers\\DirectShowVideoRenderer.*`:
  existing madVR-native implementation to mirror semantically.
