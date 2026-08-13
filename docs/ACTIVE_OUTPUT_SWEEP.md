# VP Renderer active output sweeps

These are fullscreen diagnostic runs against real capture content. They use a
generated configuration whose filename contains `active-output-sweep`; VP
refuses to run against a normal user configuration. Each case recreates the
renderer (and by default the capture graph), observes authoritative applied
state and presentation, then restores the generated configuration.

The top-right card is separate from the normal VP statistics overlay and stays
inside the active picture area. Results are based on structured renderer state,
not log-text matching:

- `PASS` (green): the requested technical contract, presenter ownership, target
  primaries, required DXGI Check/Set/Check evidence, successful submission, and
  DXGI presented-frame evidence match a case whose physical response does not
  need grading.
- `EXPECTED` (cyan): a deliberately unsupported request produced the exact safe
  fallback or block described by the case. An arbitrary fallback is a failure.
- `MEASURE` (amber): either the case exists to compare black floor, transfer,
  HDR mapping, bit depth, or another physical result, or composed/BitBlt frames
  were rendered and submitted without authoritative display-delivery evidence.
  Grade it visually or with a meter; VP must not claim that logs prove it.
- `FAIL` (red): the applied contract contradicts the expected contract, required
  DXGI evidence is absent, rendering is unexpectedly blocked/fallback, or no
  successful submission is observed before the timeout.

Each result includes actual range, pixel transfer, target primaries, presenter
owner, acceptance/verification flags, submission count, renderer-readback
state, display-delivery evidence, actual swapchain format, and the DXGI
declaration. A nonblack backbuffer proves renderer content only; it does not
prove that DWM or the physical display showed the frame. The same assertion is
written to the renderer log for audit.

## Shared controls

`/active_output_sweep` starts a sweep. `/active_output_sweep_suite sdr|hdr`
selects the suite; SDR is the default. `/active_output_sweep_hold_ms` defaults
to 10000 and accepts 1000 through 600000 milliseconds. Use
`/active_output_sweep_tests 2,5` or `2-5,8` for a subset. The default runs the
whole selected suite. `/active_output_sweep_restart capture|renderer` defaults
to `capture`; `renderer` keeps capture live but still recreates the renderer,
D3D11 device, and swapchain.

## SDR output-transport suite

Run this while SDR material is live. The default suite has 10 non-redundant
cases covering the shipping legacy path, the VP-owned DXGI path, guarded Full
and Limited pure-Gamma-2.2 behavior, Limited Gamma 2.4, an 8-bit control, and
composed presentation. Gamma 2.0 rejection and compute/shader-cache permutations
remain covered by deterministic unit/configuration tests; they were removed
from the slow fullscreen default because they add no new output contract.

The Full-range projector comparison is cases 3 and 4. Case 3 must produce the
documented Full/sRGB fallback; case 4 enables the strict calibrated-display
override and renders pure Gamma 2.2 pixels under the nominal Full-G22/sRGB DXGI
declaration. The Limited-range comparison is cases 5 and 6. Cases 4, 6, 7, and
8 deliberately report `MEASURE`: metadata can prove which pixels VP rendered,
but only the display or an instrument can prove the resulting curve and floor.

| Test | Expected technical result | What the tester evaluates |
| --- | --- | --- |
| 1 | Legacy Flip, Full, sRGB, Present | Shipping baseline. |
| 2 | VP Flip, Full, sRGB, verified DXGI, Present | VP-owned baseline and stability. |
| 3 | Exact fallback to legacy Full/sRGB | Full pure-2.2 guard-off policy. |
| 4 | VP Flip, Full, pure 2.2, verified nominal Full-G22 declaration | Curve/black floor against madVR. |
| 5 | Exact fallback to legacy Full/sRGB | Limited pure-2.2 guard-off policy. |
| 6 | VP Flip, Limited, pure 2.2, verified DXGI | Projector Limited-range curve and floor. |
| 7 | VP Flip, Limited, pure 2.4, verified DXGI | Lifted-black control against test 6. |
| 8 | VP Flip, Full, sRGB, 8-bit request | Banding/levels against 10-bit test 2. |
| 9 | libplacebo bitblt, Full, sRGB, submitted; delivery unverified | Visually grade composed-path delivery; never automatic PASS. |
| 10 | libplacebo bitblt despite VP-owned request; delivery unverified | Confirms documented Composed ownership and requires visual grading. |

## HDR tone-mapping suite

Run this only with real HDR content. It refuses to start unless VP currently
reports a valid PQ, HLG, or HDR input EOTF. The launcher asks whether this run
targets Rec.709 or BT.2020 and whether it should send the BT.2020 HDMI
InfoFrame; that selected color/signaling path is held constant across every
case. Structured assertions verify the configured target primaries; the suite
never alters incoming HDR metadata.

| Test | Change from the HDR baseline | Purpose |
| --- | --- | --- |
| 1 | 100 nits | Conventional low target reference. |
| 2 | 200 nits | Reported safe-boundary comparison. |
| 3 | 250 nits | First just-above-boundary test. |
| 4 | 300 nits | Direct fullscreen anchor for control comparisons. |
| 5 | 400 nits | Higher-target stress comparison. |
| 6 | Legacy Full/sRGB | Comparison against the Full pure-2.2 anchor. |
| 7 | Full pure 2.2 with guard off | Must fall back exactly to legacy Full/sRGB. |
| 8 | VP-owned Full pure 2.2 | Repeats the 300-nit technical anchor. |
| 9 | Limited pure 2.2 with guard off | Must fall back exactly to legacy Full/sRGB. |
| 10 | VP-owned Limited pure 2.2 | Black-floor/range comparison. |
| 11 | VP-owned Limited pure 2.4 | Lifted-black curve control. |
| 12 | Composed Full/sRGB | Fullscreen/windowed compositor comparison. |
| 13 | 300 nits + BT.2390 | Alternate highlight roll-off. |
| 14 | 300 nits + Reinhard | Alternate compression baseline. |
| 15 | 300 nits + softclip gamut mapping | Tests boundary color compression. |
| 16 | 300 nits + peak detection off | Identifies dynamic peak-analysis influence. |
| 17 | 300 nits + contrast recovery 0.0 | Identifies local-contrast recovery influence. |

Tests 1 through 5 isolate the reported target-nits threshold. Tests 6 through
12 cover the non-redundant output/black-floor contracts with HDR input. Tests
13 through 17 change one mapping control at the 300-nit Full-pure-2.2 anchor.
All HDR cases report `MEASURE` after their technical assertions succeed. Record
the visual/meter result and preserve the test number, OSD description, and log.
