# VP-0125: Authoritative D3D11/DXGI presenter

## Objective

Make the experimental VP-owned output path genuinely authoritative. VP must own
the DXGI swapchain, its backbuffer lifetime, colour-space application, Present,
failure recovery, and presentation telemetry. libplacebo remains responsible for
image processing and for rendering encoded pixels into the target supplied by VP.

The first required proof is SDR Rec.709, limited range, pure power Gamma 2.2 on
a 10-bit flip-model swapchain. The design must also preserve a safe Full/sRGB
fallback and leave a clean path to HDR/PQ and BT.2020.

The second proof covers displays calibrated to Full-range pure power Gamma 2.2.
DXGI has no declaration that expresses that exact Rec.709 transfer. Microsoft
defines `DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709` as standard sRGB: a linear
toe followed by a 2.4-power segment, only approximately Gamma 2.2. VP therefore
models this proof as two deliberately separate facts:

* rendered pixels: `PL_COLOR_TRC_GAMMA22` (pure power 2.2);
* DXGI declaration: `RGB_FULL_G22_NONE_P709` (nominal sRGB/G22).

This calibrated-display override is opt-in, VP-owned Direct presentation only,
and wire state remains unverified. It must never be described as an exact or
verified DXGI pure-2.2 mapping.

## Evidence from comparable renderers

### MPC Video Renderer

MPC VR owns its D3D11 swapchain. It selects Flip Discard/Flip Sequential or the
legacy Discard model, acquires buffer zero, renders directly to that buffer,
applies DXGI colour-space/HDR metadata, and calls Present itself. It also owns
resize and reports the active swap effect and format in its statistics overlay.

MPC VR is not a direct policy template for this issue: its D3D11 colour
conversion defaults to full-range RGB output. Its architecture is useful; its
range policy is not.

### mpv

mpv has two instructive paths:

* The native D3D11 path owns swapchain creation, ResizeBuffers, SetColorSpace1,
  Present, GetLastPresentCount, and GetFrameStatistics. It exposes flip, sync
  interval, output format, output colour space, adapter, exclusive fullscreen,
  and composition as diagnostic/user options.
* gpu-next obtains mpv's swapchain and wraps it with
  `pl_d3d11_create_swapchain`. Rendering and Present then use libplacebo's
  `pl_swapchain_*` lifecycle. Creating the DXGI object outside libplacebo does
  not make the caller the presenter.

mpv's DXGI mapping code understands studio G22/G24 enum values, but its explicit
D3D11 output-colour-space option exposes full-range sRGB/linear/PQ/BT.2020.
Current gpu-next also exposes `--treat-srgb-as-power22`. It changes the target
transfer from libplacebo sRGB to `PL_COLOR_TRC_GAMMA22` while retaining the
ordinary Full-G22 DXGI declaration. This is the closest public precedent for
VP's calibrated-display experiment; it does not remove the need for measurement.

### Full-range pure-2.2 strict contract

The Full/pure-2.2 experiment is accepted only when all of these are true:

* Custom output profile explicitly enables the Full-G22 experiment;
* presentation is Direct and VP actually owns the flip-model presenter;
* output range is explicitly Full and output gamma is explicitly 2.2;
* `IDXGISwapChain3` advertises Present support before configuration;
* `SetColorSpace1(RGB_FULL_G22_NONE_P709)` succeeds;
* the post-configuration capability check still advertises Present support;
* the renderer target uses RGB Full levels and `PL_COLOR_TRC_GAMMA22`.

Unlike legacy unsupported requests, any failure blocks rendering for that strict
generation. It cannot silently substitute sRGB, a libplacebo-owned presenter,
or composed presentation. Logs and OSD show the pixel transfer, nominal DXGI
declaration, presenter owner, strict status, and `wire_state=unverified`
independently.

### libplacebo 7.360

`pl_d3d11_create_swapchain` may wrap a caller-created swapchain, but its
`pl_swapchain_swap_buffers` implementation still calls `IDXGISwapChain::Present`.
The wrapper is therefore not an ownership boundary.

The D3D11 backend creates its own feature-level-11 swapchains with shader-input,
render-target, and unordered-access usage. A wrapped D3D11 texture is marked
`blit_dst` at feature level 11 only when its bind flags include the storage-image
combination. The current VP experiment created its backbuffer with render-target
usage alone. The resulting wrapped target was renderable but not `blit_dst`,
violating the `pl_swapchain_frame` contract and producing the repeated validation
failure observed on the EPSON.

libplacebo's normal D3D11 swapchain colour-space chooser handles Full G22,
Full BT.2020 G22, PQ, linear/scRGB, and limited fallback cases, but does not select
RGB Studio G22/G24 as a normal output hint. VP must therefore retain explicit
DXGI policy for calibrated limited-range output.

### JRiver

JRiver/JRVR is closed source. Public documentation confirms selectable video
renderer and VideoClock behavior, but does not expose enough implementation
detail to establish swapchain ownership or its applied range/gamma policy. It is
useful as a black-box comparison target only.

## Current failure and terminology correction

The current beta path is hybrid:

1. VP creates an `IDXGISwapChain1`.
2. libplacebo wraps it as a `pl_swapchain`.
3. libplacebo acquires buffer zero and calls Present.
4. VP applies `SetColorSpace1` and reads presentation statistics around that
   libplacebo-owned lifecycle.

This must not be described as a VP-owned presenter. The accurate description is
"VP-created, libplacebo-presented swapchain." The log field
`active_readback` is also too strong: DXGI has no colour-space getter. A
successful SetColorSpace1 followed by a capability recheck is an applied-state
record, not a wire-state readback.

## Target architecture

### Ownership boundary

In the authoritative path VP owns:

* `IDXGISwapChain1/3/4` and its creation descriptor;
* `GetBuffer(0)` acquisition and release for every frame;
* wrapping the current backbuffer as a temporary `pl_tex`;
* `SetColorSpace1` and, for HDR, `SetHDRMetaData`;
* `Present`, its HRESULT, QPC timing, present ID, and frame statistics;
* occlusion handling, resize, monitor migration, device loss, and fallback;
* the applied-state record shown in logs, OSD, and configuration UI.

libplacebo owns:

* source interpretation, scaling, gamut conversion, tone mapping, dithering,
  LUTs, shaders, and overlays;
* encoding the final pixels according to the target `pl_color_repr` and
  `pl_color_space` supplied by VP;
* GPU work submission before VP presents the completed target.

The authoritative path must not call `pl_swapchain_start_frame`,
`pl_swapchain_submit_frame`, or `pl_swapchain_swap_buffers`.

### Per-frame flow

1. Verify the presenter state is Ready and the negotiated output contract is
   safe to render.
2. Acquire swapchain buffer zero and inspect its descriptor.
3. Wrap it with `pl_d3d11_wrap` and verify `renderable && blit_dst` before any
   renderer call.
4. Construct a one-plane target frame with RGB representation, the accepted
   Full/Studio range, accepted transfer, primaries, bit depth, luminance, LUT,
   crop, and overlays.
5. Render with `pl_render_image`.
6. Flush libplacebo GPU work, destroy the temporary wrapper, and release all
   native backbuffer references.
7. Call VP's `Present(sync_interval, flags)` exactly once.
8. Record Present HRESULT, block time, present ID, frame statistics, selected
   monitor, and the applied-state generation.

Buffer zero is reacquired each frame. No backbuffer, RTV, or `pl_tex` reference
may survive ResizeBuffers or swapchain destruction.

### Swapchain creation

The first vertical slice uses:

* `CreateSwapChainForHwnd`;
* `DXGI_FORMAT_R10G10B10A2_UNORM`, with an 8-bit diagnostic override;
* `DXGI_SWAP_EFFECT_FLIP_DISCARD` and three buffers;
* `DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT |
  DXGI_USAGE_UNORDERED_ACCESS` when supported;
* single-sample buffers, stretch scaling, ignored alpha;
* `MakeWindowAssociation` to disable implicit Alt+Enter/window changes;
* sync interval 1, no tearing in the initial correctness slice.

Before creation, VP records `CheckFormatSupport` for display, render target,
shader sample, blend, typed UAV, and texture2D capabilities. After creation, VP
records the actual descriptor and verifies the acquired buffer's native bind
flags and libplacebo capabilities.

If direct wrapping is rejected, a later compatibility slice may render into a
VP-managed intermediate texture and copy/resolve it to the swapchain buffer.
That is not the first choice because libplacebo 7.360 does not expose a public
native-texture unwrap API, and a second colour pass creates another place for
range/transfer errors.

### State machine

The presenter states are:

* Disabled: proven libplacebo-owned shipping path.
* Creating: resolving factory/output and creating the swapchain.
* Negotiating: probing and applying format/colour-space state.
* Ready: target capability contract verified; rendering is allowed.
* Occluded: Present returned occluded; use `DXGI_PRESENT_TEST` until visible.
* Recovering: bounded attempt after resize or monitor migration.
* Failed: render and Present are blocked; a safe-path renderer restart is
  requested once for this generation.

Every transition carries a monotonically increasing generation and a reason.
Repeated per-frame validation failures must be rate-limited and must never form
an unbounded log/UI starvation loop.

### Applied-state truth model

The UI and OSD distinguish:

* Requested: user/profile intent.
* Planned: VP policy result before DXGI.
* Applied record: SetColorSpace1 succeeded for this swapchain generation after
  `CheckColorSpaceSupport(PRESENT)`; the exact format and descriptor are known.
* Presenting: at least one successful Present occurred after that application.
* Wire state unverified: DXGI provides no colour-space getter and does not prove
  the HDMI InfoFrame or display interpretation.
* Fallback/failed: requested and applied contracts differ, with a concise reason.

Never label the applied record as a colour-space readback.

## Reinitialization policy

### No teardown

These are renderer-only and can remain live when the target contract is
unchanged: tone-map operator parameters, target nits, scaler choice, debanding,
dithering, shader/LUT content where the existing renderer already supports safe
replacement, and diagnostic overlay duration/selection.

### Re-negotiate without ResizeBuffers

SetColorSpace1 and HDR metadata may be reapplied on the existing swapchain only
when format, dimensions, swap effect, flags, and buffer count remain compatible.
The applied-state generation changes and the next successful Present confirms
the transition is in use.

### ResizeBuffers

Window/client-size changes on the same device/format release every target
reference, call ResizeBuffers, reapply colour space/metadata, and revalidate the
first acquired buffer before returning to Ready.

### Recreate swapchain and renderer target state

Changes to presentation model, output format/bit depth, buffer count, flags,
composition/HWND path, fullscreen mode, or VP-owned/libplacebo-owned authority
require swapchain recreation. The current hard capture-and-renderer reinit is
appropriate for the experimental UI until the presenter state machine has
independent synchronization coverage.

### Full D3D11 device teardown

Device removed/reset/hung, adapter change, unrecoverable format-support change,
or repeated creation/resize failure requires destruction of renderer, all
wrapped/native resources, swapchain, libplacebo D3D11 context, and D3D11 device.

## Failure containment and fallback

* Capability mismatch is detected before `pl_render_image`; it is not allowed
  to reach libplacebo's repetitive validation path.
* A frame may call Present only after a successful render.
* One failed authoritative generation requests one hard fallback restart to the
  libplacebo-owned Legacy path. It does not silently continue with a different
  range or gamma inside the same generation.
* The OSD/configuration UI shows `FAILED` or `FALLBACK` prominently and includes
  the requested and actual contracts.
* The Proposed profile remains on the shipping presenter until the
  authoritative path passes the EPSON matrix. Custom is the only profile that
  can enable the experiment.

## Focused validation matrix

The first automated/native matrix is deliberately small and diagnostic:

1. Flip, Full, sRGB/G22, 10-bit: safe baseline.
2. Flip, Limited, pure G22, 10-bit: primary EPSON proof.
3. Flip, Limited, G24, 10-bit: distinguish transfer behavior.
4. Flip, Limited, pure G22, 8-bit: format sensitivity.
5. Flip, Full, G22, 8-bit: fallback baseline.
6. Monitor migration and resize with no retained backbuffer references.
7. Unsupported colour-space injection: no render, one fallback transition.
8. Forced target-capability rejection: no Present and no error flood.
9. Present failure/device-loss injection: one full recovery request.
10. HDR/PQ/BT.2020 in a separate suite after the SDR authority proof.

Each case records requested, planned, applied record, swapchain descriptor,
backbuffer descriptor/capabilities, Present result/count, frame statistics,
fallback reason, and visual/calibration observation. Acceptance of an API call
is not a visual pass.

## Delivery sequence

1. Correct names and logs; add a pre-render target capability gate and
   rate-limited failure containment to the existing experiment.
2. Match libplacebo's required external-buffer usage flags and prove the current
   validation flood is eliminated.
3. Replace the hybrid `pl_swapchain_*` frame lifecycle with VP acquisition,
   temporary texture wrapping, libplacebo rendering, VP Present, and VP timing.
4. Add resize, occlusion, monitor migration, and device-loss recovery.
5. Integrate requested/planned/applied/presenting/fallback status into OSD and
   configuration UI.
6. Run the SDR EPSON suite, then the HDR suite, before considering the
   authoritative path for Proposed defaults.
