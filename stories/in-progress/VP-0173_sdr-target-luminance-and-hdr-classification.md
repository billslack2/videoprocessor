# VP-0173: Decouple SDR target luminance from HDR classification

## Status

In Progress (2026-09-08). Implementation on `codex/vp-0173-sdr-luminance`
in `E:\codex\videoprocessor\vp-0173-sdr-luminance`, based on current
`origin/v1.3.005-beta` at `d663773cedabcfa9e9c197046839ea99dd1ed965`.

User clarification: no separate SDR brightness setting. Preserve SDR input
apart from scaling and explicitly enabled color processing. Existing
`sdr_target_nits` / `sdr_black_nits` remain compatible configuration keys for
the HDR-to-SDR tone-map destination; SDR uses a fixed internal reference.

Verified bundled libplacebo c3a3d203 DLL classifies SDR max_luma > 203 as HDR.
Upstream transport fix 4dbc490b0770539942abb3cc61fdce5438d06331 is absent.
Implement VP-side containment at both existing hint call sites through one
helper, preserving the fork and its analysis-crop ABI. GPU readback, scaling,
gamut-mismatch, transport, configuration validation and Release build pending.

## User story

As a VP Renderer operator with a bright calibrated SDR display, I want to set
its real SDR peak luminance without changing SDR content classification,
scaling behavior, peak detection, or display signaling, so calibration metadata
does not accidentally make SDR behave as HDR.

## Product contract

For an SDR source, SDR target/display luminance is display metadata, not an HDR
classification input. Raising it above the libplacebo SDR-white threshold must
not enable HDR-only renderer behavior, alter SDR scaler selection or peak
detection, negotiate PQ/BT.2020 transport, or otherwise change a matched SDR
presentation.

For an HDR source, the configured target luminance remains the legitimate
tone-map destination. This story must preserve HDR tone mapping and avoid
conflating the SDR display-peak role with the HDR tone-map target role.

## Scope

1. Rebase or otherwise integrate the applicable upstream libplacebo D3D11
   swapchain correction that bases HDR transport decisions on transfer curve,
   rather than target `max_luma`. If the fork update is deferred, implement and
   test an equivalent VP-side containment at the sole swapchain-hint boundary.
2. On the VP SDR render path, prevent `sdr_target_nits` from populating source
   or destination luminance in a way that classifies an SDR frame as HDR.
   Keep SDR source/target luminance at SDR white for the render-path
   classification inputs while preserving the configured display metadata where
   it is genuinely needed.
3. Review the setting model and editor validation. Values above the existing
   ceiling must either be accepted safely or rejected explicitly in the UI and
   documentation; they must never silently revert to a default value.
4. Measure SDR gamut-mismatch cases (for example BT.2020 or P3 SDR into a
   Rec.709 target) before finalizing the render-path treatment, because
   perceptual gamut mapping can consume target luminance even when tone mapping
   is an identity.
5. Add diagnostics and focused tests that distinguish source transfer,
   source/target luminance, selected swapchain format/colorspace, and the
   renderer's HDR-classification decision.

## Non-goals

- Changing HDR source classification or HDR tone-map destination behavior.
- A broad redesign of display calibration, gamut mapping, or the renderer's
  HDR policy beyond the SDR-luminance boundary.
- Claiming byte-identical gamut-mismatched SDR output without measured evidence.

## Acceptance criteria

- On a matched SDR unity chain, 75, 203, and 400 nit settings produce
  byte-identical readback output; sigmoidization, linear-light scaling, and
  peak detection are unchanged across those values.
- At every accepted SDR target luminance, the negotiated SDR swapchain remains
  the configured SDR transfer/colorspace; diagnostic forcing of an 8-bit SDR
  swapchain continues to hold above 203 nits.
- HDR source tone mapping to a target above 203 nits remains unchanged by the
  SDR correction.
- SDR BT.2020/P3-to-Rec.709 gamut-mismatch output is measured before and after;
  any intentional difference is documented and reviewed rather than assumed
  neutral.
- Out-of-range luminance input is either accepted under a documented safe
  contract or visibly rejected with the retained effective value; it is never
  silently substituted.
- Focused unit/GPU tests and x64 Release build pass, with libplacebo fork
  revision and any upstream cherry-pick/rebase recorded in validation evidence.

## Dependencies and readiness

- Confirm whether the pinned libplacebo fork contains the relevant upstream
  D3D11 swapchain fix and record the exact commit relationship. Do not assume a
  source-only change can repair transport behavior if the fork remains older.
- Trace all current writes of `sdr_target_nits`, the current setting validation,
  and the final D3D11 colorspace/metadata path on the beta base before editing.
- Establish readback and DXGI-log bench fixtures for matched SDR, SDR
  gamut-mismatch, and HDR inputs before declaring the behavior proven.
