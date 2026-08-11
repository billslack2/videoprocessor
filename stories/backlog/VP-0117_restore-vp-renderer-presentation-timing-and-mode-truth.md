# VP-0117: Restore VP Renderer presentation timing and mode truth

## Status

Backlog. Reproduced on deployed `v1.2.001-beta` commit `b2e1956` on
2026-08-11. VP Renderer used a 2560x1440 fullscreen host but reported
`Transport Actual: Blt/F/sRGB/G22/709`. Its presentation telemetry remained
`evidence=0`, `presented=0`, `present_id=0`, and `display_hz=0` across every
five-second sample. The OSD consequently showed a renderer delay while
`Delay` and `Present` remained `---` indefinitely.

## User story

As a VideoProcessor operator, I want VP Renderer to use the correct
presentation model for its active display mode and expose trustworthy
presentation timing, so the OSD can report complete latency when the platform
supports it and clearly explain when it cannot.

## Problem statement

`IDXGISwapChain::GetFrameStatistics` is sampled after each submission, but the
current BitBlt presentation model does not produce usable evidence on this
borderless fullscreen path. It is not yet established whether BitBlt is an
unintended negotiation regression, a required output/color fallback, or a
supported mode whose timing source must come from another API. The fix must
resolve that contract rather than fabricate presentation timestamps.

## Scope

1. Reproduce and classify VP Renderer's swapchain in normal windowed,
   windowed-fullscreen/borderless, and any supported exclusive/direct mode.
   Record HWND style/state, DXGI fullscreen state, swap effect, buffer count,
   flags, requested presentation policy, actual presentation model, and the
   HRESULT returned by each timing API.
2. Determine why a fullscreen VP host resolves to `Blt`. If flip presentation
   is safe and intended for that mode, restore it without regressing SDR/HDR
   encoding, range, primaries, output signaling, resizing, renderer handoff,
   or first-frame reveal. If BitBlt is required, use another authoritative
   timing source or explicitly mark presentation timing unsupported for that
   exact mode.
3. Make telemetry failure diagnosable. Do not collapse unsupported,
   disjoint/rewarming, and unexpected API failure into the same permanent
   zero-valued state.
4. Publish presentation and capture-to-presentation latency only from
   generation-current, monotonic evidence tied to the submitted source frame.
   Preserve `---` rather than showing a guessed number when evidence is not
   authoritative.
5. Keep the OSD presentation-model label truthful and concise. `Blt` versus
   `Flip` describes DXGI presentation, not whether the VP host merely covers
   the monitor.

## Acceptance criteria

- A mode matrix test or diagnostic harness proves the actual swapchain model
  and timing capability for normal windowed and both fullscreen choices.
- In every supported fullscreen mode, stable live playback eventually reports
  nonzero, advancing presentation IDs/evidence and fills `Present` plus total
  `Delay` with frame-correlated values.
- If a mode is inherently unsupported, its OSD remains `---` and diagnostics
  name the presentation model and reason; no permanent silent `evidence=0`
  loop remains.
- Fullscreen/windowed transitions reset stale presentation evidence and
  reacquire it for the new renderer generation without leaking prior values.
- Flip/BitBlt selection changes do not regress output encoding/range contracts,
  LUT/shader operation, resize, cursor behavior, capture continuity, or safe
  first-frame reveal.
- Focused policy/telemetry tests cover supported evidence, unsupported mode,
  disjoint rewarm, API failure, generation reset, and OSD availability. A clean
  x64 Release build and live fullscreen validation pass.

## Non-goals

- Inventing a latency estimate from queue depth or swap-call duration.
- Changing the meaning of VP-owned ingress/renderer latency.
- Enabling cadence correction merely because presentation diagnostics become
  available; that remains governed by its own policy.

## Dependencies and references

- VP-0024: Alpha source-to-display timing and queue telemetry.
- VP-0027: display-verified Alpha cadence evidence.
- VP-0100/VP-0101: pixel-owned output and calibrated presentation contracts.

