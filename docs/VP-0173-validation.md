# VP-0173: SDR input preservation

## Contract and implementation

Per the user's clarified contract, no separate SDR luminance control is added.
SDR is not forced into literal passthrough: selected debanding and other
non-tone-mapping processing still apply. The unity test disables optional
processing only to isolate this change; a separate test enables debanding
and verifies its effect is retained independently of HDR destination nits.
The existing `sdr_target_nits` and `sdr_black_nits` keys remain compatible and
specify only the HDR-to-SDR tone-mapping destination. SDR source and target
metadata use 203 / 0.203 nits internally; these reference values do not set
physical display brightness. Source/target transfer, primaries, range,
explicit gamma adjustment, calibration LUT and scaling settings are retained.
Stale SDR HDR metadata is cleared, including scene and mastering metadata.
HDR source metadata is unchanged and its destination uses the configured nits.

Both previous swapchain-hint call sites now use one submission method and a
copy of transport-only metadata. SDR transfer hints cannot become HDR because
of configured luminance. The VP-owned presenter retains its existing control
of DXGI state. HDR-transfer hints are unchanged.

The editor labels the controls as HDR tone-map white/black, rejects invalid
values continuously, and retains the saved configuration. The existing white
range remains 40 through 500 nits; there is no new numeric limit in libplacebo.
Black remains Auto or a nonnegative value below destination white. Auto uses
white / 1000. The internal SDR reference ignores both destination settings.

`LUMINANCE_CONTRACT` diagnostics report source/target transfer, luminance and
effective-HDR decisions, configured HDR destination, negotiated texture format,
DXGI contract and peak eligibility when those inputs change.

## Baseline and dependency

- Source base: `origin/v1.3.005-beta`,
  `d663773cedabcfa9e9c197046839ea99dd1ed965` (queried and fetched 2026-09-08).
- Bundled libplacebo: 7.360.1 plus VP-0147 analysis crop, fork `c3a3d203`.
- DLL SHA-256: `D2BCC6E62DF86760825639949448594D69024C0C2544D0DFC3D6C58D05E23507`.
- Upstream transport correction: [4dbc490b0770539942abb3cc61fdce5438d06331](https://github.com/haasn/libplacebo/commit/4dbc490b0770539942abb3cc61fdce5438d06331).
  The pinned source still uses `pl_color_space_is_hdr` in the three affected
  D3D11 decisions. VP-side containment is used; no DLL update or cherry-pick.
- Direct calls to the pinned DLL confirmed SDR classification is false at
  203 nits and true at 203.01, 400 and 500 nits before normalization.

## Validation

Release build and focused regression results are recorded below after execution.
GPU fixtures use the actual bundled libplacebo D3D11 backend with WARP and a
64x64 RGBA8 gradient, with optional debanding and dithering disabled for exact
readback. Scaling cases use 32x32, 64x64 and 96x96 destinations and the native
high-quality scaler/sigmoid/peak parameters. These are repeatable software-GPU
checks, not physical HDMI/projector measurements. No deployment is performed.
