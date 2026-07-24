# VP-0008: Alpha renderer presentation-pacing assessment and refinement

## Status

Draft investigation. This is deliberately later than VP-0005/0006 because
libplacebo/DXGI swapchain presentation may already provide correct vblank pacing.
Do not add a second scheduler unless measurements demonstrate a real defect.

## Context

The alpha render loop calls `m_impl->Render`, which renders, submits, then calls
`pl_swapchain_swap_buffers`. This is normally the component that blocks/paces
presentation relative to the display. Adding sleeps or an independent QPC pacing
loop around it without evidence could create double pacing, higher latency, or
missed vblanks.

The renderer already supports display refresh switching and has stable lifecycle
methods. This story is about validating and, only when justified, refining
presentation timing; it is not a replacement for the existing refresh-switch
logic or DirectShow timestamp modes.

## User story

As an alpha-renderer tester, I need evidence that presentation is synchronized to
the selected display refresh rate and a narrowly scoped corrective option only if
the swapchain's behavior is proven insufficient on real target systems.

## Investigation and implementation plan

1. Consume VP-0005 measurements to compare input cadence, display rate,
   render duration, swap/present blocking duration, queue trend, and actual
   dropped/repeated-frame events across 23.976, 24, 50, 59.94, and 60 Hz.
2. Record presentation model, tearing/vsync flags, swapchain configuration, and
   DWM/DXGI timing diagnostics where available. Clearly distinguish measured
   facts from estimates.
3. Establish pass criteria before modifying code:
   - queue depth remains stable at matched rates;
   - no periodic queue-pressure drops attributable to early presentation;
   - present timing is consistent with the selected refresh;
   - latency remains within the alpha baseline.
4. If the criteria pass, close this story as assessed with no pacing code change.
5. If they fail reproducibly, prototype exactly one opt-in renderer-native policy
   behind an experimental config setting, such as a presentation queue depth or
   swapchain latency mode. Do not add QPC `Sleep` pacing as a first response.
6. Validate each prototype independently against the baseline. Keep a setting
   only when it improves measured stability without increasing drops or latency.
7. Any phase-based control must remain optional and consume the existing
   `SetSceneTimingPhase` data only after its units and physical meaning are
   validated on more than one system.

## Verification

- Run sustained matched-rate and deliberately mismatched-rate sessions at each
  target refresh rate.
- Include display refresh switches, HDR/SDR metadata changes, F2/F3 profile
  switches, OSD on/off, window/fullscreen transitions, and renderer rebuilds.
- Preserve logs and configuration for every comparison so results can be
  reproduced.

## Acceptance criteria

- There is a documented data-backed decision: existing swapchain pacing is
  sufficient, or one specific opt-in refinement is retained.
- No speculative global timing scheduler is introduced.
- DirectShow start/stop timestamp methods remain unavailable and irrelevant to
  alpha renderer presentation.
