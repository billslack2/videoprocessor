# VP-0109: Validate and support Alpha pure-2.2 Studio limited output

## Status

Backlog (readiness review refined 2026-08-09). The reported policy defect is
confirmed, but the original story incorrectly treated DXGI Studio/G22 and
libplacebo pure gamma 2.2 as mathematically identical. They are not.

Implementation is gated by VP-0109-1, which must prove that the intentional
pairing of a `PL_COLOR_TRC_GAMMA22` render target with the
`RGB_STUDIO_G22_NONE_P709` limited transport produces the required application
codes and display response on the affected chain. VP-0109-2 owns production
implementation after that decision is accepted.

The VideoProcessor repository default branch discovered during this review is
`v1.2.001-beta` at `25f6203`. No implementation branch or worktree has been
selected. Before VP-0109-1 or VP-0109-2 source work begins, re-query the default
branch and obtain developer confirmation of the implementation base.

## User story

As an Alpha projector user whose display expects limited RGB and is calibrated
to pure power gamma 2.2, I want an explicit `output_range=limited` plus
`output_gamma=2.2` mode that renders pure-2.2 Studio RGB codes and uses the
available Studio/G22 DXGI transport, so the display receives the intended
range and response without a source-transfer workaround or a silent Full/sRGB
fallback.

## Evidence and confirmed defects

The supplied three-case report used an RTX 3060 Ti at 3840x2160p23.976 with
NVIDIA Full output fixed, a projector configured for limited RGB and pure
gamma 2.2, limited Rec.709 v210 input, and lossless P210 handoff into Alpha.
The observations are strong controlled visual corroboration, not photometric
or on-wire measurements.

1. Auto/Auto selected composed Full/sRGB. Backbuffer readback contained values
   below 64 and above 940, consistent with clipping on the limited display.
2. Limited/Auto selected Studio/G24 and produced application codes inside
   64..940, but the image was too bright near black on the gamma-2.2 display.
   Changing only `sdr_input_transfer` from 2.4 to 2.6 moved the result visually
   toward madVR. This strongly implicates a transfer mismatch but does not by
   itself exclude every driver, compositor, or display transform.
3. Limited/2.2 created a flip swapchain on which Studio/G22 was present and
   overlay-capable, but `MakePlan()` rejected Gamma 2.2. `Finalize()` then
   defaulted to Full/sRGB, and the AUTO-presentation fallback recreated a
   composed Full/sRGB swapchain.

The source confirms both software defects:

- `MakePlan()` accepts only Auto and Gamma 2.4 in its Limited branch even
  though the Studio/G22 enum and DXGI mapping already exist.
- Invalid or failed Limited plans are represented by a default Full/sRGB
  `Actual`, and AUTO+Limited explicitly falls back to composed Full/sRGB.

`sdr_input_transfer=2.6` is a diagnostic workaround only and must be reset
before validation of a corrected output path.

## Contract vocabulary and standards decision

These terms must remain separate in code, logs, tests, documentation, and OSD:

- **Display target:** a display calibrated to a pure-power 2.2 EOTF.
- **Renderer target transfer:** libplacebo `PL_COLOR_TRC_GAMMA22`. In bundled
  libplacebo 7.360.1/API 360, its source implements `pow(x, 2.2)` for
  linearization and `pow(x, 1/2.2)` for delinearization.
- **Quantization:** limited RGB, nominally 64..940 for each 10-bit RGB channel.
- **DXGI transport declaration:**
  `DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709`. Microsoft describes this as
  nominal BT.709 with a linear segment; it is not an exact pure-power-2.2
  transfer declaration.
- **Application-code evidence:** values read from the R10 swapchain backbuffer
  before presentation.
- **API acceptance evidence:** active-swapchain capability plus successful
  `CheckColorSpaceSupport` / `SetColorSpace1` / support recheck. The post-check
  is not a current-state getter.
- **Wire evidence:** independently captured output codes/signaling, if suitable
  HDMI analysis equipment is available.
- **Display-response evidence:** luminance measurements from the affected
  projector chain.

This story intentionally evaluates pure-2.2-rendered values carried in the
named Studio/G22 limited transport for the known chain. It must not claim that
DXGI and libplacebo use identical transfer definitions, that API acceptance
proves wire state, or that backbuffer readback proves display response.

## Current libplacebo and VP architecture

Bundled libplacebo 7.360.1 does not natively select a Studio RGB swapchain for
a Gamma-2.2 hint. Its D3D11 backend chooses Full/G22 P709 with sRGB output
metadata for non-wide SDR, and `pl_swapchain_start_frame()` reports Full RGB.
VP implements Studio output as an external contract:

1. Give libplacebo its creation hint and perform all required resizes.
2. Unwrap the DXGI swapchain.
3. Probe and set the Studio color space through `IDXGISwapChain3`.
4. Override the returned frame target with VP-owned transfer, levels, and
   primaries metadata before `pl_render_image()`.

The production implementation must make the VP DXGI set the final color-space
mutation after every libplacebo hint/resize. It must not depend on libplacebo's
cached Full/sRGB mapping preventing a later overwrite. Compatibility is pinned
to libplacebo 7.360.1/API 360 and must be revalidated on any library upgrade.

VP-0093 also established that an SDR BT.2020 render target uses the proven P709
DXGI transport plus the existing NVIDIA AVI signaling path; P2020 transport
failed on the projector chain. VP-0109 must preserve that separation. A
BT.2020 render target may use pure gamma 2.2 and Studio levels, but its DXGI
transport remains Studio/G22 P709 unless separate hardware evidence deliberately
reopens VP-0093.

## Required configuration policy

1. Explicit Limited/2.2 with Rec.709 target plans a flip candidate, pure Gamma
   2.2 renderer target, limited levels, and Studio/G22 P709 DXGI transport.
2. Explicit Limited/2.2 with an SDR BT.2020 render target preserves BT.2020
   render primaries and the existing AVI-reporting policy while using the P709
   DXGI transport established by VP-0093.
3. Full/2.2 is invalid. Full/G22 P709 denotes the sRGB transport, not an exact
   pure-power-2.2 contract.
4. Composed/Limited is invalid because a BitBlt swapchain cannot satisfy the
   Studio contract. Do not silently override either explicit setting.
5. Auto-range/2.2 remains invalid in this story. The hardware-specific
   interpretation must require explicit Limited range rather than silently
   opting every Gamma-2.2 request into it.
6. Limited/Auto remains the current Studio/G24 policy, and Limited/2.4 remains
   supported. Other undeclarable gamma requests remain invalid.
7. Invalid configuration is rejected before mutating DXGI state. A live
   configuration apply preserves the previously accepted configuration under
   VP-0103 semantics and surfaces an actionable error.

## Rejection-safe transition model

Replace the stateless default-to-Full result with an explicit transition model
that carries configured request, resolved plan, prior active contract,
swapchain/device generation, API evidence, and one result action:
`KeepPrior`, `ApplyDesired`, `RestorePrior`, or `FailClosed`.

The required order is deterministic:

1. If the desired contract is already API-accepted on the same swapchain
   generation and no validity boundary changed, keep it without resetting to
   Full first.
2. Otherwise perform all libplacebo hint/resize work, then Check/Set/Check the
   desired Studio contract as the final color-space mutation.
3. A failure before mutation may retain the prior API-accepted limited contract
   only on the same still-valid generation.
4. A failure after attempted mutation must Check/Set/Check rollback to the
   prior limited contract. If rollback cannot be accepted, fail closed.
5. A new swapchain, resize that recreates buffers, renderer/fullscreen
   reconstruction, refresh/display/monitor change, device loss, or teardown
   invalidates prior acceptance. No contract from an older generation may be
   retained.
6. Gamma 2.4 is not an automatic fallback for an explicit Gamma-2.2 request.
   Full/sRGB is never a renderable fallback for an explicit Limited request.
   When the requested limited contract cannot be established or restored,
   stop submitting frames and report the recovery action.

Requested, planned, prior, active/retained, API-accepted, application-code,
wire, and display-response states must never be collapsed into one flag.

## Implementation scope

1. Add `TargetTransfer::GAMMA22` and explicit contract/result state to
   `LibplaceboOutputPolicy`.
2. Centralize each encoding's DXGI declaration, renderer transfer, levels,
   transport primaries, render primaries, and diagnostic label in one testable
   description. Studio/G22 uses Gamma 2.2 and limited levels and must never
   pass through UNKNOWN-to-sRGB substitution.
3. Implement the transactional transition and generation invalidation model
   above under the renderer's existing `renderMutex` ownership.
4. Apply metadata consistently to the VP render target, display-LUT target
   validation, R10 diagnostics, and immutable/published OSD state. Log the
   libplacebo-returned target separately from the VP override.
5. Make every color-space support line self-describing: swapchain model,
   numeric swap effect, buffer count, actual format, HRESULT, flags, and
   whether the result is meaningful for that active model. A BitBlt result is
   not a verdict about a later flip swapchain.
6. Update `CONFIGURATION.html` and `VideoProcessor.cfg` with an opt-in
   Limited/2.2 example, standards caveat, P709 transport behavior, invalid
   combinations, capability/failure behavior, preview exception, and the
   prohibition on the 2.6 input-transfer workaround.
7. Preserve Auto/Full sRGB, Limited/Auto and Limited/2.4, preview's intentional
   composed Full/sRGB behavior, BT.2020 target/AVI signaling, display LUTs,
   refresh/recovery behavior, and madVR semantics.

Likely production paths on the current default branch are:

- `src\VideoProcessor-Lib\vprenderer\LibplaceboOutputPolicy.{h,cpp}`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`
- `src\VideoProcessor-Test\LibplaceboOutputPolicyTests.cpp`
- `src\VideoProcessor-Test\ConfigFileTests.cpp`
- `CONFIGURATION.html`
- `VideoProcessor.cfg`

## Test plan

### Policy and state-machine tests

- Full matrix for presentation x range x gamma x render primaries, including
  Limited/2.2, Full/2.2, Auto-range/2.2, Composed/Limited, Limited/Auto,
  Limited/2.4, and unsupported gamma values.
- BT.2020 render target retains BT.2020 target primaries while the transport
  remains P709 and existing AVI policy remains unchanged.
- Missing `IDXGISwapChain3`, pre-check unsupported, Set failure, post-check
  unsupported, rollback success/failure, same-contract idempotence, and
  generation invalidation.
- No result can report a Full contract as active for a Limited request.
- Exact mocked call order: libplacebo hint/resize precedes the final VP DXGI
  Set; rollback targets the prior contract rather than unconditionally Full.

### Renderer metadata and GPU-code tests

- Each encoding maps to exact levels, renderer transfer, render primaries, and
  transport. Gamma22 must never become UNKNOWN, sRGB, BT.1886, or Gamma24.
- Display-LUT validation accepts Gamma22/limited/correct primaries and rejects
  wrong transfer, range, or primaries.
- Use a known linear-light neutral pattern with scaling, LUTs, tone mapping,
  dithering, and nonzero black disabled. Expected 10-bit limited codes are
  `round(64 + 876 * pow(L, 1/2.2))`; require +/-1 code:

  | Relative linear light | Pure 2.2 code | BT.709-piecewise code |
  | ---: | ---: | ---: |
  | 0.001 | 102 | 68 |
  | 0.010 | 172 | 103 |
  | 0.018 | 205 | 135 |
  | 0.180 | 466 | 422 |
  | 0.500 | 703 | 682 |
  | 1.000 | 940 | 940 |

- Assert the actual backbuffer is `DXGI_FORMAT_R10G10B10A2_UNORM`; black is
  64/64/64, white is 940/940/940, the neutral ramp is monotonic, alpha is
  excluded, and studio-valid samples remain within 64..940.
- With dithering enabled, verify bounds and neighborhood means rather than
  exact pixels. Separately test and document clamp behavior for footroom,
  headroom, and illegal source values.
- Repeat with shader cache cold, warm, and disabled, and through
  G24->G22, G22->Full, resize, fullscreen, refresh, renderer rebuild, and
  device-loss recovery. Generated codes must not change with cache state.
- PQ and HLG inputs remain tone-mapped to SDR; source HDR metadata may affect
  tone mapping but never the chosen SDR Studio/G22 output contract.
- Retain one v210/P210 chroma-edge and one interlaced fixture as non-blocking
  regressions; use neutral progressive patterns for transfer proof.

### Hardware and affected-chain validation

1. Preserve and hash the original configuration and current/rotated VP logs.
   Keep GPU driver, NVIDIA Full range, projector mode, Windows color state,
   cable path, resolution, refresh, and source-decode settings fixed.
2. Validate Full/2.2 and Auto-range/2.2 rejection; Auto/Auto and Limited/2.4
   regressions; Limited/2.2 acquisition; preview; fullscreen exit/re-entry;
   refresh switch; renderer restart; and display/device recovery.
3. Record backbuffer codes as application evidence and Check/Set/Check as API
   evidence. If available, use an HDMI analyzer/capture for wire codes and
   signaling; do not infer wire state from DXGI alone.
4. Measure projector black plus 1%, 2%, 5%, 10%, 18%, 50%, 75%, and 100% gray.
   After black subtraction, fit 10..90% response and require gamma 2.20 +/-
   0.05. Require 18%/50% luminance within 3% of the repeatable madVR/reference
   result, subject to meter repeatability.
5. Compare near-black, midtone, black, near-white, and white patterns with
   madVR. Record visual results as corroboration, not calibration.

### Build and automated test execution

From a clean, developer-confirmed feature worktree, build and test x64 Release:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  .\VideoProcessor.sln /t:Rebuild /m `
  /p:Configuration=Release /p:Platform=x64

& 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\Extensions\TestPlatform\vstest.console.exe' `
  .\x64\Release\VideoProcessor-Test.dll
```

Do not deploy during VP-0109-1. Any later deployment must use paired executable
and renderer-plugin artifacts from the same successful x64 Release build and
must preserve active configuration values/comments.

## Acceptance criteria

1. VP-0109-1 records a reproducible decision that the deliberate pure-2.2
   renderer plus Studio/G22 transport pairing is acceptable for production,
   or this implementation task is stopped/superseded without mislabeling the
   standards.
2. Explicit Limited/2.2 produces the numeric pure-2.2 R10 application codes,
   uses limited levels and a Studio/G22 P709 API-accepted transport on the
   active flip swapchain, and reports renderer and DXGI semantics separately.
3. Failed acquisition or renegotiation retains/restores an API-accepted prior
   limited contract on the same valid generation or fails closed. It never
   renders Full/sRGB for the explicit Limited request.
4. BT.2020 render-target behavior preserves VP-0093's P709 transport and AVI
   signaling architecture; no P2020 DXGI transport is introduced by this
   story.
5. Automated policy, transition, metadata, LUT, diagnostic, cache, and
   regression tests pass in a clean x64 Release build.
6. Affected-chain measurements meet the recorded gamma and luminance criteria,
   and application, API, optional wire, photometric, and visual evidence are
   labeled according to what each can prove.
7. Documentation and sample configuration describe the opt-in mode, invalid
   combinations, standards caveat, failure behavior, and recovery action.

## Decomposition

1. **VP-0109-1 - Prove the pure-2.2 renderer/Studio-G22 transport pairing.**
   Bounded non-production spike. It locks the libplacebo/DXGI contract,
   operation ordering, numeric code expectations, and affected-chain evidence.
2. **VP-0109-2 - Implement rejection-safe pure-2.2 Studio limited output.**
   Production policy, state machine, renderer metadata, diagnostics,
   documentation, and regression coverage. Depends on accepted VP-0109-1.

The root closes only after both children are Done and the cross-task hardware
and regression acceptance above is recorded.

## Non-goals

- Arbitrary power gammas, custom DXGI transports, P3-D65, or a new
  calibration/3D-LUT workflow. VP-0166 owns the full-range calibration path;
  limited-range LUT activation remains a deliberately separate extension.
- Changing Alpha ingress range handling; VP-0096 owns the independent ingress
  mismatch.
- Changing the DirectShow nominal-range control, madVR rendering, NVIDIA global
  range, deployed configuration, or projector settings.
- Treating enum presence, Check/Set/Check, backbuffer readback, screenshots, or
  visual comparison as proof of a different evidence class.

## Dependencies and relationships

- VP-0004, VP-0011, VP-0012, VP-0019, VP-0064, and VP-0093 established output,
  LUT, signaling, and recovery safeguards retained here.
- VP-0093 is authoritative for BT.2020 target pixels over P709 DXGI transport
  plus optional NVIDIA AVI signaling.
- VP-0096 remains responsible for the independent Alpha ingress-range issue.
- VP-0166 remains the calibration-LUT path, but its v1 contract rejects Studio
  range until an explicit normalization or storage-code-authored extension is
  separately approved and proven.
- VP-0103 supplies live-configuration rejection/rollback semantics.
- Source evidence:
  `C:\Users\bslac\Downloads\alpha_output_range_gamma_findings (2).pdf`.
- Primary technical references: Microsoft `DXGI_COLOR_SPACE_TYPE` and
  `IDXGISwapChain3::CheckColorSpaceSupport`; libplacebo 7.360.1
  `colorspace.{h,c}`, `renderer.c`, `d3d11/swapchain.c`, and `swapchain.h`.
