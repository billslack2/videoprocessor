# VP-0100: Prove pixel-owned SDR presentation for Alpha

## Status

Backlog. This is a bounded architecture and validation spike. It must finish
with reproducible evidence and a go/no-go decision before VP-0101 can expose a
production pixel-owned output mode or accept calibration contracts that do not
match a named DXGI color space.

No implementation branch has been selected. Before source work starts, query
the current default branch of `billslack2/videoprocessor`, report it to the
developer, and obtain explicit confirmation of that implementation base under
the tracker workflow.

## User story

As a calibrated-projector user, I need Alpha to prove whether it can write
exact renderer-selected SDR gamma, gamut, and range code values and deliver
them unchanged to the physical display, so direct calibrated output and later
3D LUT output are not constrained to an approximate or mismatched desktop
color contract.

## Decision this spike must make

Can Alpha support a bounded, initially NVIDIA-specific presentation mode in
which libplacebo owns the final SDR encoding and the Windows/DXGI/GPU path
preserves those code values through presentation and physical output?

A successful result must identify the exact supported presentation mode,
swapchain format and declaration, Windows state, driver state, quantization
range, and verification procedure. A failed result must identify where values
are changed and retain VP-0011's current contract-locked output as the only
supported production path.

## Current architecture and problem

VP-0011 and VP-0012 deliberately built a safe contract-locked calibration
path. `LibplaceboOutputPolicy.cpp` accepts only output combinations represented
by a supported DXGI declaration. `LibplaceboVideoRenderer.cpp` then replaces
requested target metadata with the accepted swapchain encoding before render;
requested settings never flow directly into the target. Display-LUT
activation additionally requires the target and accepted output signal to
match, and `LibplaceboDisplayLut.cpp` rejects P3.

Those guardrails prevent silent mismatch, but they also block valid calibration
domains that DXGI cannot name. Full G22/P709 is the sRGB piecewise transfer,
not an arbitrary pure gamma 2.2 curve. DXGI has no named P3-D65 SDR color
space. Libplacebo itself already supports BT.1886, sRGB, pure power gamma
1.8-2.8, BT.709, BT.2020, and Display P3; the unresolved boundary is delivery
after libplacebo renders.

The DXGI declaration must not simply be treated as decorative metadata.
`SetColorSpace1` defines how the swapchain color data is interpreted, and a
GPU-driver `Full` range setting proves only the selected quantization range,
not the absence of transfer, gamut, compositor, or display-engine processing.
This story therefore requires end-to-end evidence rather than removal of the
current guardrail by assumption.

## Contract vocabulary

The spike and all diagnostics must keep these domains distinct:

1. **Source contract**: captured sample format, matrix, primaries, transfer,
   range, chroma location, frame size/rate, and HDR metadata.
2. **Render target / LUT-input contract**: the exact encoded RGB values Alpha
   asks libplacebo to produce before an optional target-frame calibration LUT.
3. **Post-LUT native-drive contract**: values emitted by a calibration LUT for
   the projector's selected native/calibrated mode. These are not automatically
   P3, BT.2020, or Rec.709 merely because the LUT input used one of those
   reference spaces.
4. **Presentation transport contract**: swapchain format, color-space tag,
   presentation mode, Windows color state, and GPU-driver settings used to
   carry renderer-owned values.
5. **Physical output/signaling contract**: measured RGB code values and any
   HDMI/DisplayPort range, primaries, transfer, or InfoFrame signaling observed
   after the GPU.

## Spike scope

1. Add an isolated test path or harness that can generate deterministic RGB
   ramps, patches, near-black/near-white values, and diagnostic cubes without
   depending on live capture conversion. Use live capture only after synthetic
   output behavior is understood.
2. Inventory candidate D3D11 presentation combinations available to Alpha:
   swapchain format, buffer model, windowed/composed presentation, borderless
   fullscreen, exclusive or independent-flip behavior where actually
   available, and every relevant `CheckColorSpaceSupport`/`SetColorSpace1`
   result. Do not assume modes with similar names behave identically.
3. Test an NVIDIA-first matrix with Windows HDR/Advanced Color state, ICC/color
   management state, driver output format, driver full/limited range, display
   mode, refresh rate, and renderer fullscreen state recorded. AMD and Intel
   are explicitly unverified unless separately measured.
4. Ask libplacebo to generate at least these distinguishable SDR encodings:
   sRGB, pure gamma 2.2, pure gamma 2.4, and BT.1886; Rec.709 and BT.2020
   primaries; full and limited range. Include P3-D65/gamma-2.2 as the critical
   currently undeclarable reference target.
5. Capture/read back the rendered backbuffer to prove what Alpha/libplacebo
   generated. Treat this as necessary but insufficient evidence because later
   presentation stages may still alter the values.
6. Independently verify physical output using a trustworthy HDMI capture path,
   analyzer, calibrated measurement workflow, or an equally reproducible
   method capable of detecting range remapping and transfer/gamut changes.
   Record fixture details, expected values, tolerances, and actual results.
7. Test no LUT, identity target LUT, and a diagnostic non-identity target LUT.
   Establish whether the post-LUT values are preserved without an additional
   transfer, range, gamut, tone-map, or color-management pass.
8. Determine whether any usable DXGI declaration can function as a transparent
   transport for renderer-owned code values under a strictly defined supported
   environment. `DXGI_COLOR_SPACE_CUSTOM` is not proof by itself; the spike
   must show how the presented values are interpreted and preserved.
9. Exercise initialization, fullscreen/window transitions, refresh changes,
   display reconnect, renderer restart, GPU reset, Windows HDR changes, and
   teardown. Identify which changes invalidate proof and require output-policy
   reacquisition before rendering resumes.
10. Preserve the current Check/Set/Check contract-locked mode unchanged as the
    fallback. A candidate pixel-owned mode must fail closed and explain why it
    was unavailable rather than silently using mismatched output metadata.

## Non-goals

- Do not expose or enable a production pixel-owned mode in this spike.
- Do not claim that NVIDIA Full range means universal bit-perfect passthrough.
- Do not add AMD/Intel support without equivalent evidence.
- Do not implement the production P3/3D-LUT configuration, profile rules, or
  documentation owned by VP-0101.
- Do not add 1D-LUT chains, ICC workflows, arbitrary shader ordering, or an
  external calibration application.
- Do not weaken VP-0011's safe no-LUT fallback or existing Rec.709/BT.2020
  contract-locked behavior.
- Do not change deployed user configuration or projector settings without
  explicit approval.

## Required evidence and deliverables

1. A result matrix for every tested combination, including GPU/driver,
   Windows build/state, display path, swapchain format/tag, presentation mode,
   driver range, requested render encoding, backbuffer result, physical-output
   result, and pass/fail reason.
2. Reproducible test patterns and expected values covering endpoints,
   near-black, near-white, midtones, neutral ramps, primary/secondary colors,
   and values that distinguish sRGB from pure gamma 2.2 and 2.4.
3. A precise statement of what code-value preservation means and the accepted
   tolerance for each tested bit depth/transport. A successful path must show
   no systematic range expansion/compression, transfer remapping, gamut
   conversion, clipping, or double encoding.
4. Lifecycle and fallback traces proving that unsupported or invalidated states
   return to the contract-locked path without a stale frame, wrong signaling,
   black screen, retry loop, or unrecoverable renderer failure.
5. A final decision record selecting one of:
   - a supported NVIDIA-first pixel-owned transport contract that unblocks
     VP-0101;
   - a narrower path with explicit limitations that the developer accepts; or
   - no reliable pixel-owned path, leaving VP-0101 blocked or reduced to
     contract-matched LUT improvements only.

## Acceptance criteria

- The spike proves or disproves end-to-end preservation; GPU backbuffer
  readback alone is not accepted as proof.
- Any supported result distinguishes exact render/LUT-input encoding from the
  swapchain declaration and physical display signaling without calling the
  latter merely informational.
- P3-D65 is treated as a render/LUT-input domain or calibrated display target,
  not falsely reported as a generic DXGI/HDMI P3 signal.
- Every state change that can invalidate the supported path is identified,
  logged, and assigned deterministic fallback/reverification behavior.
- VP-0011's contract-locked path remains the production default throughout the
  spike.
- The decision and evidence are sufficient for a reviewer to decide whether
  VP-0101 may move to In Progress without repeating this investigation.

## Dependencies and relationships

- VP-0011 and VP-0012 are the completed safe Phase 1 baseline.
- VP-0004, VP-0019, VP-0064, and VP-0093 define existing range, SDR BT.2020,
  persistence, signaling, and fallback behavior that must not regress.
- VP-0096 improves source/conversion range correctness. Synthetic patterns let
  this spike isolate final presentation before VP-0096 completes, but VP-0101
  must not claim production end-to-end calibration until VP-0096 is accepted.
- VP-0029, VP-0047, and VP-0048 are superseded historical inputs.
- VP-0101 is blocked from implementation until this story records an accepted
  supported presentation contract.

## References

- `src\VideoProcessor-Lib\vprenderer\LibplaceboOutputPolicy.cpp`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboDisplayLut.cpp`
- `3rdparty\libplacebo\include\libplacebo\colorspace.h`
- `3rdparty\libplacebo\include\libplacebo\renderer.h`
- [DXGI color-space definitions](https://learn.microsoft.com/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type)
- [IDXGISwapChain3 color-space methods](https://learn.microsoft.com/windows/win32/api/dxgi1_4/nn-dxgi1_4-idxgiswapchain3)
