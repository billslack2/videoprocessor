# VP-0173: Decouple SDR target luminance from HDR classification

## Status

Review (2026-09-08). Implemented in draft PR
https://github.com/billslack2/videoprocessor/pull/81 against `v1.3.005-beta`.
Source commits: `b1d7eb2b` (implementation), `829493db` (validation evidence).
Current integration base: `e5f80f89a564a7600957013d2efcdfa4d7764426`.
Worktree: `E:\codex\videoprocessor\vp-0173-sdr-luminance`.

Clean x64 Release solution rebuild passes. 245/245 native tests and 2/2
Qt editor tests pass. GPU readbacks use bundled libplacebo on D3D11 WARP:
SDR unity/up/down scaling invariant at 75/203/400/500 nits, optional debanding
still applies and remains invariant, HDR 400/500-nit output unchanged, and
SDR/forced-8-bit swapchains remain SDR through 500 nits. Invalid white/black
input is visibly rejected with the saved value retained.

Final libplacebo fork: `c646b39886d6172b853eafe82e6d6e5c2aeb5de0`; corresponding
source archive inspected. It still lacks upstream 4dbc490b. VP-side hint
containment is implemented; no DLL update/cherry-pick. Exact hashes and
reproduction commands are in `docs/VP-0173-validation.md` in the PR.

P3/BT.2020-to-Rec.709 perceptual gamut mapping now consistently uses the
existing 203-nit reference behavior, independent of the HDR target. Measured
old-versus-corrected differences at 75/400 nits reach 25 8-bit codes in the
fixture; the full table is recorded for review. Physical GPU/HDMI/projector
validation and merge/release decision remain outstanding. No deployment.

User clarification: SDR is not literal passthrough. Selected debanding,
scaling and other enabled non-tone-mapping processing still apply. No separate
SDR brightness control is added; existing luminance keys control HDR-to-SDR
tone mapping only.

## User story

As a VP Renderer operator, I want SDR input to retain its intended appearance
with selected scaling, debanding and other explicitly enabled processing,
without tone mapping or changes caused by HDR destination luminance settings.

## Product contract

For SDR input, `sdr_target_nits` and `sdr_black_nits` do not control image
brightness or luminance remapping. Matched fixed internal luminance references
prevent HDR-only classification and keep SDR scaling, peak detection and
transport independent of these settings. Physical display brightness remains
controlled by the display/calibration. No separate SDR luminance setting is
needed. Existing debanding, gamma, gamut and calibration processing is retained.

For HDR input, the existing configuration keys remain the HDR-to-SDR tone-map
destination white/black controls. Preserve source HDR metadata and tone mapping.

## Scope

1. Rebase or otherwise integrate the applicable upstream libplacebo D3D11
   swapchain correction that bases HDR transport decisions on transfer curve,
   rather than target `max_luma`. If the fork update is deferred, implement and
   test an equivalent VP-side containment at the sole swapchain-hint boundary.
2. On the VP SDR render path, prevent `sdr_target_nits` from populating source
   or destination luminance in a way that classifies an SDR frame as HDR.
   Keep SDR source/target luminance at SDR white for the render-path
   classification inputs while preserving configured destination luminance for HDR input only.
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
