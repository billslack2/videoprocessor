# VP-0029: Alpha two-pass display LUT and final-dither pipeline spike

## Status

Backlog. This is a bounded design/validation spike, not a commitment to add a
second render pass. VP-0011 currently keeps a valid target 3D LUT on the
single-pass path and disables only error-diffusion dithering on D3D11 because
libplacebo 7.360.1 generates an invalid compute shader for that combination.
The safe fallback is implemented in `ccc3c06` on `VP0011+0012` and must remain
the production baseline unless this story proves a better path.

The next action is a readiness review, then default-branch discovery and user
confirmation before any VP source branch/worktree is created.

## User story

As a calibrated-display user, I want a 3D display LUT to coexist with the
full intended high-quality renderer pipeline, including final error-diffusion
dithering where it is supported, without a black screen, retry loop, silent
color-domain change, or dependence on a libplacebo upgrade.

## Problem and current boundary

The observed D3D11 failure combines a target `PL_LUT_NATIVE` 3D LUT with
libplacebo's compute-only error-diffusion pass. The generated HLSL assigns
overlapping texture registers and fails to compile. The current single-pass
compatibility policy removes only `renderParams.error_diffusion`; it preserves
target-LUT placement, normal high-quality scaling/deband/peak-detection
behavior, and no-LUT playback fallback.

This story must not upgrade, patch, fork, or replace libplacebo. It explores
whether VP can split the operation into a safe two-pass sequence using the
bundled libplacebo API.

## Unknown to resolve

Can VP render into an explicitly described intermediate target with the
display LUT applied, then render that intermediate to the swapchain with final
error-diffusion dithering, while proving that no unintended gamut, transfer,
range, tone-map, or LUT conversion occurs between the two stages?

This unknown affects color correctness, GPU lifetime, performance, recovery,
and presentation behavior. It must be resolved by the spike before any
production two-pass implementation moves to In Progress.

## Spike scope

1. Construct a minimal D3D11/libplacebo prototype using an intermediate
   renderable/sampleable texture with an explicit target color contract.
2. First pass: source image through the existing color-management pipeline and
   target-frame `PL_LUT_NATIVE` into the intermediate target, with error
   diffusion disabled for that pass.
3. Second pass: intermediate image to the real swapchain target with no LUT
   and error-diffusion enabled. The two frames must declare identical target
   primaries, transfer, range, and reference luminance so the second pass is
   an identity color transform plus final quantization/dither only.
4. Determine whether libplacebo can prove or log the identity second-pass
   contract; if not, add VP-side assertions/diagnostics sufficient to reject a
   non-identity conversion.
5. Measure 4K 23.976 and 59.94/60 Hz GPU time, texture memory, queue health,
   and recovery behavior against the current single-pass fallback.
6. Build a deterministic GPU readback harness covering identity and visibly
   non-identity LUTs, code values near black/white, full and limited target
   ranges, and Rec.709/BT.2020 contracts that the current output path can
   genuinely signal.

## Non-goals

- Do not change libplacebo version, source, build flags, or its shader
  translator.
- Do not enable unsupported P3 presentation contracts.
- Do not remove the existing single-pass safe fallback until real-display
  validation accepts a replacement.
- Do not silently use a two-pass path for a LUT whose profile contract was
  rejected.
- Do not deploy test artifacts or modify user configuration as part of this
  story without explicit approval.

## Required design decisions

- Intermediate texture format, bit depth, sampling/rendering/storability
  flags, and how its lifetime follows device/swapchain recreation.
- Exact first- and second-pass color metadata, including luminance values and
  limited/full representation.
- Whether the target LUT must operate before final dithering for calibration
  correctness, and proof that the proposed order preserves that property.
- Render/cache invalidation behavior on LUT change, display-rule change,
  output-contract fallback, resize, screen-profile switch, GPU reset, and a
  failed first or second pass.
- Explicit performance budget and a documented fallback when the intermediate
  pass cannot be allocated or misses the realtime budget.

## Validation and acceptance criteria

- The prototype has reproducible WARP and hardware test evidence showing that
  a target LUT plus final error diffusion compiles and renders on the affected
  D3D11 configuration.
- Readback proves the second pass makes no color-domain change beyond the
  intended final dithering/quantization.
- A non-identity diagnostic LUT visibly transforms only as expected; an
  identity LUT preserves the baseline within the documented dither tolerance.
- No renderer retry loop, black screen, device loss, queue starvation, or
  invalid OSD active state occurs after a pass failure.
- Performance and memory impact are recorded for the target hardware and are
  acceptable for the selected use case.
- The spike records one decision: implement a production two-pass path in a
  follow-up story, retain the single-pass compatibility fallback, or close as
  not worthwhile. No production implementation is merged by this spike alone.

## References

- VP-0011/VP-0012 combined LUT work and current fallback: `ccc3c06` on
  `VP0011+0012`.
- Current renderer: `src\\VideoProcessor-Lib\\libplacebo\\LibplaceboVideoRenderer.cpp`.
- Bundled libplacebo: 7.360.1 / API 360.
- [libplacebo D3D11 error-diffusion compile report](https://code.videolan.org/videolan/libplacebo/-/issues/228)
- [libplacebo D3D11 compute suppression API](https://code.videolan.org/videolan/libplacebo/-/tags/v7.351.0)
