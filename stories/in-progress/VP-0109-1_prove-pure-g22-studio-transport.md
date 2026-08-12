# VP-0109-1: Prove the pure-2.2 renderer/Studio-G22 transport pairing

## Status

In Progress as of 2026-08-09. The developer explicitly confirmed the current
default integration branch `v1.2.001-beta` at
`25f6203cfbef774110bcf3e0fbe7cba2c559c15f`. The bounded diagnostic work uses
branch `codex/vp-0109-1-g22-spike` in the clean worktree
`C:\Users\bslac\vp\worktrees\vp-0109-1-g22-spike`. No deployment is permitted.

## Parent

VP-0109. VP-0109-2 must not begin until this spike is accepted.

## Objective

Determine whether VP can deliberately render limited RGB with
`PL_COLOR_TRC_GAMMA22`, carry those values through
`RGB_STUDIO_G22_NONE_P709`, and obtain the intended pure-2.2 response on the
affected projector chain without claiming that the libplacebo and DXGI
transfer definitions are identical.

## Scope

1. Trace bundled libplacebo 7.360.1/API 360 from target metadata through shader
   delinearization, level encoding, D3D11 swapchain selection, and returned
   frame metadata.
2. Build the smallest diagnostic-only probe needed to produce deterministic
   pure-2.2 limited R10 values and apply the Studio/G22 P709 override after all
   libplacebo hints/resizes.
3. Capture exact application codes and API evidence on a flip swapchain.
4. Measure the affected display response against independent expectations and
   the repeatable madVR/reference result. Capture wire evidence if suitable
   HDMI equipment is available.
5. Record one explicit decision: approve the pairing for VP-0109-2, reject it,
   or defer to VP-0100/VP-0101.

The probe must not change production defaults, documentation promises,
deployed binaries, or active user configuration.

## Test procedure

- Use a linear-light neutral fixture with scaling, LUTs, tone mapping,
  dithering, and nonzero black disabled.
- Assert an actual R10G10B10A2 flip backbuffer and compare output with
  `round(64 + 876 * pow(L, 1/2.2))` at 0, 0.001, 0.01, 0.018, 0.18, 0.5, and
  1.0. Require +/-1 code and values 64..940.
- Run a contrasting BT.709-piecewise target so the near-black samples prove
  which transfer was rendered.
- Trace libplacebo hint/resize, returned Full/sRGB metadata, VP target override,
  DXGI pre-check/Set/post-check, and final application readback in order.
- Repeat cache cold, warm, and disabled, plus one resize and one renderer
  recreation.
- On the affected chain, preserve/hash baseline config/logs and hold driver,
  GPU range, display mode, color-management state, resolution, refresh, cable,
  and source decode constant.
- Measure black, 1%, 2%, 5%, 10%, 18%, 50%, 75%, and 100%. Fit 10..90% after
  black subtraction; target 2.20 +/-0.05 and repeatable 18%/50% luminance
  within 3% of madVR/reference, subject to meter repeatability.

## Acceptance criteria

1. Source analysis identifies every transfer and range operation and proves
   that `PL_COLOR_TRC_GAMMA22` generates pure-power-2.2 values in the bundled
   version.
2. The diagnostic backbuffer matches the independent pure-2.2 limited-code
   oracle and is distinguishable from BT.709, sRGB, and Gamma 2.4 near black.
3. Studio/G22 is API-accepted only after the final VP-owned Set on the active
   flip swapchain; the record does not call that wire verification.
4. Application, API, optional wire, photometric, and visual evidence are
   recorded separately with hardware/software versions and repeatable steps.
5. The decision explicitly approves or rejects the deliberate semantic
   pairing and lists any device/driver limitation. Only an approved decision
   unblocks VP-0109-2.

## Dependencies and non-goals

- Parent: VP-0109.
- Preserve VP-0093 BT.2020 target/P709 transport behavior.
- No production fallback/state-machine implementation, deployment, arbitrary
  gamma, calibration workflow, or change to VP-0096 ingress behavior.
