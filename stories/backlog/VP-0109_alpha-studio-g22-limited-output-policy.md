# VP-0109: Support verified Alpha Studio/G22 limited output and rejection-safe fallback

## Status

Backlog (2026-08-09). A third-party beta validation on a projector calibrated
for limited RGB and pure power gamma 2.2 establishes a reproducible Alpha
output-policy defect. In
`VideoProcessor-v1.1.016-beta-3-g75478cb-VP0098-x64-20260807`, the active
flip-model swapchain advertises `RGB_STUDIO_G22_NONE_P709` as present and
overlay-capable, but the policy rejects the same requested configuration one
second later and falls back to composed Full/sRGB.

This is a bounded contract-locked renderer repair, not a claim that arbitrary
pixel-owned SDR output is available. The requested Studio/G22/Rec.709 mode is
already a named DXGI declaration and must still pass the existing
Check/Set/Check capability verification on the active swapchain. No
implementation branch or worktree has been selected.

Before source work starts, query the current default branch of
`billslack2/videoprocessor`, report it to the developer, and obtain explicit
confirmation of the implementation base under the tracker workflow.

## User story

As an Alpha projector user whose display expects limited RGB and is calibrated
to pure power gamma 2.2, I want `output_range=limited` plus
`output_gamma=2.2` to select and verify the matching Studio/G22 DXGI contract,
so the renderer sends studio-range codes with the calibrated transfer curve
instead of either clipping in Full/sRGB or lifting shadows with gamma 2.4.

## Evidence and diagnosis

The supplied report is a three-case fullscreen comparison against a known-good
madVR control. The fixture was an RTX 3060 Ti feeding a 3840x2160p23.976
projector, with NVIDIA Full output held constant. The source was limited
Rec.709 v210 and Alpha ingress was lossless P210 in every cited run. This
evidence is strong visual corroboration under controlled single-variable
changes; it is not a photometric calibration result.

1. `output_range=auto` selects composed Full/sRGB. The renderer expands the
   limited source to full, producing codes below 64 and above 940 for a
   limited-range display. Its sRGB toe also is not the display's pure 2.2
   transfer.
2. `output_range=limited`, `output_gamma=auto` selects
   `RGB_STUDIO_G24_NONE_P709`. Readback remains inside the studio window, but
   the pure gamma-2.4 output is visibly too bright near black on a pure-2.2
   display. Changing only `sdr_input_transfer` from 2.4 to 2.6 compensates
   the visual error, isolating the issue to Alpha's output transfer rather
   than input levels, driver range expansion, or the compositor. This
   diagnostic workaround must not become a supported configuration.
3. `output_range=limited`, `output_gamma=2.2` first logs
   `RGB_STUDIO_G22_NONE_P709` as present and overlay-capable on the active
   flip swapchain, then rejects the request with "no matching limited-range
   DXGI declaration." The invalid plan reaches `Finalize()` before range
   handling, causing the limited request to be discarded and returning the
   user to the clipping Full/sRGB path.

The beta code makes the policy cause explicit: `MakePlan()` accepts only
`AUTO` and `GAMMA24` in its limited-range branch even though
`DxgiEncoding::STUDIO_G22_P709` exists. `EncodingTransfer()` deliberately
returns `PL_COLOR_TRC_UNKNOWN` for Studio/G22, despite libplacebo providing
`PL_COLOR_TRC_GAMMA22`. Existing output-policy tests encode this rejection as
expected behavior.

## Required output-policy behavior

1. `output_range=limited` plus `output_gamma=2.2` shall plan
   `STUDIO_G22_P709` for a Rec.709 target, `STUDIO_G22_P2020` for an existing
   BT.2020 target request, `TargetTransfer::GAMMA22`, and a flip-model
   candidate. A successful Check/Set/Check must remain mandatory before the
   plan becomes active.
2. The Studio/G22 encoding must map through every output boundary consistently:
   DXGI color-space selection, libplacebo swapchain color hint, rendered target
   metadata, diagnostics/readback labels, display-LUT contract validation, and
   OSD/status reporting. It must use `PL_COLOR_TRC_GAMMA22` and limited
   levels, never an unknown-transfer or implicit sRGB substitution.
3. `output_range=auto` with an explicit `output_gamma=2.2` shall resolve
   deterministically to the verified limited Studio/G22 candidate, with a
   diagnostic that records the AUTO-to-Limited resolution. `output_range=full`
   plus `output_gamma=2.2` remains invalid because Full/G22/Rec.709 denotes the
   sRGB transport, not an exact pure-power gamma-2.2 contract.
4. A requested limited range must never be silently converted to Full/sRGB
   merely because the requested gamma is unsupported or the Studio/G22
   transition fails. Retain an already-active verified limited contract when
   possible; otherwise use only a documented, visibly surfaced limited-safe
   fallback or fail closed with a precise diagnostic. Do not claim that a
   gamma-2.4 fallback is gamma 2.2.
5. The policy must distinguish invalid configuration from unavailable runtime
   capability. A named Studio/G22 plan that fails Check/Set/Check is a
   capability failure with the recorded evidence, not "no matching DXGI
   declaration." The configured request, policy plan, active contract, and
   fallback decision must remain separately observable.

## Scope

1. Extend `LibplaceboOutputPolicy` so Gamma 2.2 is a supported limited-range
   plan and make the explicit Auto-range resolution above testable as policy,
   not incidental swapchain behavior.
2. Add the Gamma 2.2 target transfer and plumb it through
   `EncodingTransfer()`, `SetSwapchainColorHint()`, target/representation
   matching, display-LUT validation, color-space probing, negotiation logs,
   diagnostics, and OSD text.
3. Preserve the current verified Check/Set/Check transition and Full/sRGB
   restore safeguards. A Studio/G22 request is active only after it verifies
   on the current active swapchain and is invalidated/reacquired on the same
   renderer, fullscreen, refresh, display, device, and teardown boundaries as
   the other Studio contracts.
4. Redesign the invalid-plan and runtime-capability-failure path so one invalid
   output component cannot discard an independently explicit range request.
   Record the selected safe behavior in the configuration reference, log, and
   user-visible diagnostic; no silent Full/sRGB regression is permitted.
5. Make each DXGI color-space support line self-describing: include the
   swapchain model, swap effect, buffer count, and whether that model makes a
   Studio capability result meaningful. Document that a BitBlt probe is not a
   general GPU/display capability verdict.
6. Update `CONFIGURATION.html` and the checked-in sample configuration with a
   valid Limited/2.2 example, the Auto/2.2 resolution, the invalid Full/2.2
   combination, supported-device verification requirements, failure behavior,
   and a warning never to use `sdr_input_transfer=2.6` as a production fix.
7. Add focused policy, renderer-metadata, and configuration tests. Replace the
   current tests that assert Limited/2.2 and Auto/2.2 rejection; retain tests
   rejecting undeclarable transfers and Full/pure-2.2 mismatch.
8. Validate in x64 Release with synthetic code-value patterns, then reproduce
   the beta fixture in fullscreen on the affected display. Preserve the
   tester's original configuration/logs as baseline evidence and do not modify
   their driver, projector, or source-decode workaround without approval.

## Acceptance criteria

1. A Limited/2.2 Rec.709 request plans `RGB_STUDIO_G22_NONE_P709`, targets
   `PL_COLOR_TRC_GAMMA22`, uses limited RGB levels, and becomes active only
   after active-swapchain Check/Set/Check evidence passes. Existing BT.2020
   target selection receives the analogous Studio/G22 contract only when that
   capability verifies.
2. An Auto/2.2 request emits one deterministic resolution record and follows
   the same verified Studio/G22 path. A Full/2.2 request is rejected with an
   actionable explanation; it is never mislabeled as pure gamma 2.2.
3. Successful Limited/2.2 R10 diagnostics show no sampled channel below 64 or
   above 940 for studio-valid test patterns, identify Studio/G22 and
   `GAMMA22`, and preserve the chosen primaries. The renderer target,
   swapchain hint, and display-LUT comparison all agree on transfer and range.
4. A configuration error, missing `IDXGISwapChain3`, failed support check,
   failed `SetColorSpace1`, or failed post-set verification cannot silently
   replace an explicit Limited request with composed Full/sRGB. Logs/OSD state
   the requested values, active/retained/fallback contract, exact reason, and
   recovery action.
5. Policy tests cover Limited/2.2 and Auto/2.2 success plans; supported and
   failed Check/Set/Check; Full/2.2 rejection; unsupported limited gamma;
   pre-existing active limited preservation; and Rec.709/BT.2020 target
   branches. Renderer tests cover transfer/level metadata, diagnostics, and
   no accidental sRGB substitution.
6. The diagnostic probe output identifies its swapchain model on every
   color-space support result and documented tests demonstrate why a composed
   BitBlt `present=0` does not disprove later flip-model Studio support.
7. A clean x64 Release build and relevant test suite pass. On the affected
   fullscreen projector chain, the corrected Limited/2.2 configuration is
   accepted as Studio/G22, keeps studio codes in range, removes the need for
   the input-transfer workaround, and is visually compared with madVR using
   near-black, midtone, black, and white reference patterns. Record the
   hardware/driver/display mode and evidence without calling the visual result
   a calibration measurement.
8. Auto/Full sRGB behavior, existing Limited/2.4 behavior, BT.2020 output
   signaling, preview's intentional composed Full/sRGB contract, renderer
   recovery, and madVR behavior retain their established semantics unless this
   story's explicit policy requires otherwise.

## Non-goals

- Do not implement arbitrary power gammas, custom DXGI transports, P3-D65,
  pixel-owned output, or new calibration/3D-LUT workflow. VP-0100 and VP-0101
  own the broader investigation and production work for targets without a
  proven named presentation contract.
- Do not treat the presence of `RGB_STUDIO_G22_NONE_P709` in an enum, log, or
  a different swapchain model as proof that it works on every GPU/display.
- Do not change Alpha ingress range handling; the reported source-side
  4..63-to-64..940 issue remains VP-0096.
- Do not alter the DirectShow-only nominal-range control, madVR rendering,
  global NVIDIA range setting, deployed configuration, or projector settings.

## Dependencies and relationships

- VP-0004, VP-0011, VP-0012, VP-0019, VP-0064, and VP-0093 established the
  current output-contract, LUT, signaling, and recovery safeguards to retain.
- VP-0096 owns the independent Alpha ingress range mismatch. This story must
  keep source and output range evidence distinct and must not close VP-0096.
- VP-0100/VP-0101 remain the required path for arbitrary or non-DXGI-named
  calibrated targets. This story is narrower: Studio/G22/Rec.709 is already a
  named DXGI contract and must not be delayed on a pixel-owned-output claim.
- Source evidence: `C:\Users\bslac\Downloads\alpha_output_range_gamma_findings (2).pdf`
  (three-page report, beta build dated 2026-08-07).
- Likely code/test locations:
  `src\VideoProcessor-Lib\libplacebo\LibplaceboOutputPolicy.{h,cpp}`,
  `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`,
  `src\VideoProcessor-Test\LibplaceboOutputPolicyTests.cpp`,
  `CONFIGURATION.html`, and `VideoProcessor.cfg`.
