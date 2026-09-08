# VP-0174: Gamma 2.2 without LUT — architectural review

Reviewed 2026-09-08 after deployment of commit 033f5cc3 from beta base 38e7508f.

## Assessment

High confidence in the intended internal pure-Gamma-2.2 path for both Full and Limited. No internal double-gamma conversion or duplicate range expansion/compression was found. Confidence in identical physical display response is conditional: Windows, driver, HDMI and display behavior have not been measured in this review. The approved Limited 2.2 beta default remains reasonable; it is not a certification of every output path.

## Pipeline traced

| Stage | Full + display 2.2 | Limited + transport 2.2 + display 2.2 |
| --- | --- | --- |
| SDR interpretation | Explicit SDR input 2.2 and adjustment On honor Gamma 2.2 | Same |
| HDR interpretation | Retains HDR source transfer; tone maps to target nits/black | Same |
| Display target | Explicit PL_COLOR_TRC_GAMMA22, independent of carrier declaration | Same |
| LUT disabled | Target LUT cleared; no LUT transform | Same |
| Output representation | Full levels from accepted encoding | Limited levels from accepted encoding |
| Presentation declaration | DXGI Full G22/P709 | DXGI Studio G22/P709, requiring derived beta flag |
| Final rendering | One pl_render_image; no subsequent VP gamma conversion | Same |

For matching SDR input/output 2.2 and primaries, decoding and encoding should preserve the nonlinear signal apart from enabled processing, quantization and range conversion. This assumes the configured input transfer describes the captured signal correctly; selecting 2.2 does not prove the source itself is 2.2. HDR tone mapping is intentionally different. Target nits/black affect HDR-to-SDR output; VP-0173 prevents those controls from changing ordinary SDR brightness.

Source anchors in E:\codex\videoprocessor\vp-0174-config-ui:
- src/VideoProcessor-Lib/vprenderer/LibplaceboOutputPolicy.cpp: MakePlan, MakeSdrOutputContract, Finalize, ResolveSdrGamma.
- src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp: EncodingLevels/ResolvedPixelTransfer; ConfigureDisplayLutForTarget at 6702; SDR decision at 9526; explicit calibration target at 9719–9728; LUT attachment at 9758; final render at 11553.

## Concerns and confidence limits

1. **The DXGI declaration is not an exact pure-2.2 contract.** Microsoft defines Full G22/P709 as piecewise sRGB and Studio G22/P709 as piecewise BT.709. VP intentionally generates calibrated pure-2.2 pixels using those available declarations. A downstream conversion based on the declaration can therefore change the intended response. The mismatch exists in both paths and is especially relevant to comparing Limited against Full. SetColorSpace1 acceptance establishes API support, not preserved optical gamma. [Microsoft DXGI definitions](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type)
2. **Flip model is a presentation request, not proof of bypassing composition.** Windows can use composition, independent flip or overlays and transition between them. The renamed UI is accurate; the live status cannot certify which physical presentation route ultimately occurred. [Microsoft flip-model guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)
3. **Range agreement remains a whole-chain requirement.** VP uses accepted encoding to choose output levels, which avoids retaining Limited pixel packing after a Full fallback. It cannot prove that GPU output conversion and the display's range setting agree. Check live requested/effective status; a fallback is not a successful Limited test.
4. **Tests cover components, not the complete physical chain.** Policy tests explicitly cover Full pure 2.2 and gated Limited pure 2.2. WARP/libplacebo readbacks cover no-LUT baselines, normalized LUT/range ordering, target-gamma coordinates, SDR luminance invariance and HDR tone mapping. They do not constitute a dedicated no-LUT Full-versus-Limited 2.2 grayscale sweep through the deployed swapchain, and they do not capture HDMI. This is the main numerical coverage gap.
5. **Legacy settings remain deliberate.** Deployment preserved the active configuration. Old Auto/omitted values and conflicting flags are not silently migrated; the Config UI makes them visible. SDR adjustment Off retains legacy carrier reinterpretation semantics. Confidence in explicit 2.2/On does not extend automatically to those alternate choices.

## Follow-up validation with the greatest value

Add a no-LUT grayscale readback sweep for Full/Limited 2.2, including near-black and mid-gray, with deterministic processing and supported 8-/10-bit targets. Verify one pure-2.2 encode and one level mapping, then compare rendered results after normalizing range. Separately capture or meter Full/Limited patches on representative GPU/display setups, including presentation transitions. Internal readback and physical-chain measurements should be reported separately. These are beta confidence improvements, not newly imposed release gates.

## Deployment evidence

Successfully built x64 Release; native suite passed 1,102 tests; final complete Config UI suite passed all 65 scenarios. Matching host and renderer DLL plus Config UI deployed to C:\Videoprocessor\vp and hashes verified against staged build artifacts. Backup: C:\Videoprocessor\vp\backup-before-vp0174-20260908-101306. Active configuration SHA256 remained 69CDA6889972E43D80EFB2D24B01A4A9D4528041384F3E21923D4943252EE9EA. No new physical display measurement was performed. Independent specialist review agreed with the architectural conclusion above.

## Post-refactor assessment (2c0e0223, 2026-09-08)

High confidence in the internal implementation remains justified, now supported by
numerical 8/10-bit Full/Limited GPU ramps. A 33-point grayscale sweep preserves
2.2-to-2.2 codes (within one code value) and matches pow(x, 2.4/2.2) for a 2.4
reference displayed at 2.2 (within two code values). A converting 33-cube LUT fed
its matching 2.4 input domain agrees within two codes with direct conversion;
applying both conversions produces a detectably darker midpoint. This closes the
component-level no-LUT sweep gap identified above. These are actual libplacebo
texture readbacks on WARP, not deployed swapchain/HDMI or optical measurements.

The corrected semantic contract is explicit. SDR reference/intended response is
not the Rec.709 camera OETF. Matching 2.2 values intentionally produce a 2.2 response;
preserving a 2.4 reference on a physical 2.2 display requires conversion. At ideal
zero black BT.1886 reduces to gamma 2.4; the new pure-power tests do not establish
all finite-black BT.1886 calibration behavior. Saved gamma choices remain intact.

With a usable LUT, calibration_lut_input_gamma selects pre-LUT encoding; omission
or display retains the former behavior. Missing/disabled/rejected-without-fallback
LUTs use physical display gamma. Last-known-good LUT retention remains attached
and therefore keeps the declared LUT input domain. Explicit passthrough reinterprets
SDR in that same target domain. There is one LUT attachment and no later VP gamma
correction. Legacy Off stays carrier-based for compatibility. The independent
specialist reviewed these exact paths and found no blocker.

The physical-chain concerns above remain: Full G22 declares piecewise sRGB and
Studio G22 piecewise BT.709, so acceptance alone cannot guarantee a pure-power
2.2 optical result or Full/Limited equivalence. Present mode, driver conversions,
GPU/display range agreement and calibration still require real setup feedback.
We can proceed with the approved beta behavior while stating this limit clearly.

All 16 prior Color/Output keys are retained in the unified profile or inactive
legacy archives; runtime and UI share the migration plan. Build/test/deployment
records for this follow-up are in the current VP-0174 story. No active user
configuration values were changed during deployment.
