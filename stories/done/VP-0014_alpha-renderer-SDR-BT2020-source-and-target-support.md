# VP-0014: Alpha renderer SDR BT.2020 source and target support

## Status

Done — duplicate / will not do separately.

This story is fully superseded by VP-0019, which covers the implemented
alpha-renderer SDR BT.2020 display profiles, F5/F6 selection, target signaling,
repeated switching, renderer reconstruction, deployment, and hardware
validation. No separate implementation should be started for VP-0014. Future
BT.2020 gaps should be added as follow-up stories linked to VP-0019.

## User story

As a projector user with an SDR BT.2020/cinema-filter mode, I want the alpha
renderer to correctly accept genuine SDR BT.2020 input and optionally render
to an SDR BT.2020 target. I need source primaries, output target primaries,
transfer, range, and Windows/DXGI signaling to be explicit and independently
reported, so VP does not silently treat the signal as HDR/LLDV, Rec.709, or a
generic "wide gamut" mode.

## Why this is separate work

BT.2020 specifies primaries and related video representation; it does not by
itself mean HDR. SDR BT.2020 uses an SDR transfer curve and can be valid input
or output. The current alpha renderer uses a Rec.709 SDR output target, so it
does not expose a true SDR BT.2020 target selection.

This feature has three different concepts that must never be conflated:

| Concept | Meaning |
| --- | --- |
| Source primaries | The color primaries of captured video, for example SDR BT.2020. This is supplied by capture metadata and determines decoding/conversion. |
| Render target primaries | The requested display/reference gamut, for example Rec.709, P3-D65, or BT.2020. Libplacebo maps decoded source colors to this target. |
| Presentation signal | The color space/range/transfer accepted by the D3D11/DXGI swapchain and ultimately the display chain. It is not guaranteed merely because VP requested it. |

A BT.2020 render target is not a request to invent saturation for Rec.709
content, nor a generic pass-through mode. Rec.709 source rendered to a BT.2020
target must retain its original colors inside the larger gamut. BT.2020 source
rendered to Rec.709/P3 must use the selected gamut mapping; BT.2020 source
rendered to BT.2020 should avoid unnecessary gamut reduction.

## Current risk: SDR BT.2020 versus LLDV

VP currently has LLDV-style handling that can use an SDR + BT.2020 state as a
heuristic. A genuine SDR BT.2020 signal can have the same broad metadata shape
as such a heuristic. Therefore:

- never infer HDR, PQ, or LLDV solely from `EOTF=SDR` and
  `primaries=BT.2020`;
- never tone map a known SDR BT.2020 source merely because it is wide gamut;
- do not make a display rule such as `EOTF=SDR && primaries=BT.2020` silently
  choose LLDV behavior;
- establish an explicit capture classification, reliable metadata discriminator,
  or user override/profile choice before automatic SDR-BT.2020 rules are
  enabled.

If the capture path cannot distinguish true SDR BT.2020 from the configured
LLDV heuristic, the safe initial behavior is a manual display profile/override
with clear OSD/log reporting, not an automatic guess.

## Scope

Alpha/libplacebo renderer only:

- `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`
- `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.h`
- `VideoProcessorRenderer.cfg`
- `VideoProcessorRenderer.html` and `VideoProcessorRenderer-Alpha.html`
- renderer OSD/detail and diagnostic logging.

The feature must cover both:

1. accurate interpretation of a captured SDR BT.2020 source; and
2. opt-in rendering to an SDR BT.2020 display/reference target.

The default remains the existing Rec.709 SDR target. Do not change the color
of existing Rec.709, HDR-to-SDR, LLDV, or no-configuration playback.

## Constraints and researched findings

1. The bundled libplacebo API represents source/target color spaces separately
   through `pl_color_space` and can color-map between primaries. The current VP
   renderer must stop constructing a hard-coded Rec.709 target before it can
   expose a true BT.2020 target.
2. DXGI defines SDR BT.2020 RGB color spaces, including full-range G22
   `DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020`, studio-range G22
   `DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020`, and studio-range G24
   `DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020`. Their existence is not proof
   that a particular GPU, driver, DWM presentation mode, AVR, or projector
   accepts/preserves them. VP must use `CheckColorSpaceSupport`, attempt
   `SetColorSpace1`, record the result, and retain a safe fallback.
3. DWM-composed presentation and existing VP output-range work make the active
   wire/display contract distinct from the requested swapchain declaration.
   VP-0004 remains the authority for proving limited-range behavior. Initial
   BT.2020 support may be full-range-only if that is the only demonstrated
   presentation path.
4. MPC Video Renderer is useful comparative evidence because it is a
   DirectShow renderer with shader processing, HDR support, HDR-to-SDR
   conversion, and HDR metadata transfer. Its public documentation does not
   define a portable SDR BT.2020 target contract for VP to copy. Treat MPC as
   an interoperability test/reference, not as proof that VP may signal BT.2020
   on every Windows presentation path.

## Independent architecture review

An implementation-focused review of the current working branch confirms the
following boundary:

- `TranslatePrimaries` already maps `ColorSpace::BT_2020` to
  `PL_COLOR_PRIM_BT_2020`, and `TranslateColorSpace` therefore has a path to
  represent SDR BT.2020 source metadata. `TranslateTransfer(EOTF::SDR)` uses
  BT.1886 unless the existing explicit SDR-input-transfer comparison override
  is selected.
- The configured alpha output is still built from
  `pl_color_space_bt709`; the current output plan negotiates presentation/range
  and gamma but has no target-primaries dimension. Source support alone is not
  SDR BT.2020 output support.
- The bundled headers provide `PL_COLOR_PRIM_BT_2020`, but their predefined
  spaces include HDR10 and BT.2020 HLG variants rather than a ready-made SDR
  BT.2020 target. The implementation must deliberately construct an SDR target
  from BT.2020 primaries plus the selected SDR transfer/nits/range. It must not
  reuse an HDR10 or HLG predefined color space for SDR content.
- This makes `target_primaries` a required output-plan input, not merely an OSD
  label or a DXGI color-space preference. The libplacebo target and DXGI
  request must derive from the same validated target contract.

References reviewed while writing this story:

- libplacebo bundled headers:
  `3rdparty\libplacebo\include\libplacebo\colorspace.h`,
  `renderer.h`, and `swapchain.h`;
- [libplacebo project](https://code.videolan.org/videolan/libplacebo) and its
  documented color-space/swapchain API evolution;
- [Microsoft DXGI_COLOR_SPACE_TYPE documentation](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type);
- [MPC Video Renderer project documentation](https://github.com/Aleksoid1978/VideoRenderer).

## Proposed configuration model (subject to validation)

Use the existing `[display]` and `[display_rules.name]` inheritance model. Do
not add an unrelated configuration section and do not overload `output_gamma`
to mean source transfer.

```ini
[display]
# Existing safe default
target_primaries=REC_709
output_gamma=AUTO
output_range=AUTO

[display_rules.sdr_bt2020_projector]
# Enable automatically only after the input classifier can distinguish this
# source from the configured LLDV heuristic. Until then, make this manual.
rule=$input_class==SDR_BT2020
priority=200
target_primaries=BT_2020
output_gamma=2.2
output_range=FULL
gamut_mapping=perceptual
```

`target_primaries` should initially accept only `REC_709`, `P3_D65`, and
`BT_2020`. `AUTO` is allowed only if its exact resolution is documented and
logged. The selected source classification and target must be displayed
separately. The example `$input_class` is intentional design notation; it is
not a claim that the current rule engine exposes that variable.

## Required investigation and implementation plan

1. **Source classification**
   - Trace `VideoState` from capture through `TranslateColorSpace` for SDR
     BT.2020, SDR Rec.709, PQ/BT.2020, HLG/BT.2020, and current LLDV-style
     states.
   - Define a durable `input_class`/equivalent or a manual override that makes
     genuine SDR BT.2020 and LLDV-style handling mutually unambiguous.
   - Preserve source transfer separately from output transfer. SDR BT.2020 is
     decoded as declared SDR, not PQ/HLG and not automatically tone mapped.
2. **Target color contract**
   - Add a validated target-primaries setting to the existing base/rule loader.
   - Construct the libplacebo target `pl_color_space` from target primaries,
     output transfer, range, and nits. Do not hard-code
     `pl_color_space_bt709` when `BT_2020` is selected.
   - Make gamut mapping conditional on a real source-to-target reduction. Do
     not gamut-compress BT.2020 source merely because it is SDR or expand
     Rec.709 source merely because the target is BT.2020.
3. **DXGI/DWM presentation**
   - Extend the existing color-space probe to try only compatible P2020 RGB
     declarations for the requested full/studio range and G22/G24 transfer.
   - Log `requested`, `advertised`, `SetColorSpace1` result, and the final
     selected fallback. Never render BT.2020 code values while leaving the
     swapchain declared as P709.
   - If P2020 cannot be accepted, select a documented safe fallback (normally
     color-convert to Rec.709/P709) and report `BT.2020 target unavailable`.
     Do not silently mis-signal the image.
4. **Rules, lifecycle, and diagnostics**
   - A target-primaries rule change is a renderer-output change and must follow
     the existing safe rebuild lifecycle; it must not mutate queued frames in
     place.
   - OSD/log output must include source EOTF/primaries, source classification,
     selected target primaries/transfer/range/nits, gamut-map mode, requested
     DXGI color space, accepted/fallback result, active display rule, and
     whether LLDV interpretation is active.
   - Add clear validation errors for impossible values or conflicting manual
     SDR-BT.2020/LLDV selections. Default safely to the existing path.
5. **Documentation and examples**
   - Explain that a projector cinema filter or wide-gamut picture mode does not
     itself prove Windows is receiving a P2020 signal.
   - Supply a manual profile example first. Add an automatic profile only when
     the input-class discriminator is implemented and tested.
   - Document full versus limited range separately and link the limited path to
     VP-0004.

## Verification

- Add deterministic color tests/readback for known SDR BT.2020 and Rec.709
  primaries. Verify SDR BT.2020 source to a BT.2020 target preserves expected
  values apart from intentional range/precision conversion.
- Verify SDR BT.2020 source to Rec.709/P3 takes the declared color-conversion
  and gamut-map path without HDR tone mapping.
- Verify Rec.709 source to BT.2020 target does not add saturation or alter
  neutral grayscale.
- Test PQ/HLG BT.2020, genuine SDR BT.2020, and LLDV-style states in rapid
  source transitions. Confirm they select only their intended rule and do not
  create renderer restart loops or queue drains.
- On target hardware, capture DXGI probe logs for full and limited range,
  validate projector cinema-filter behavior with color patches/grayscale, and
  confirm the OSD’s requested/accepted presentation state matches the logs.
- Test unsupported P2020 signaling. Confirm VP continues with the documented
  safe fallback and a visible actionable diagnostic.
- Regression-test the default Rec.709 target and HDR-to-SDR path; their output
  and no-configuration behavior must remain unchanged.

## Acceptance criteria

- Alpha distinguishes source SDR BT.2020 from HDR/HLG/PQ and from any active
  LLDV heuristic, using evidence or an explicit manual choice rather than a
  silent heuristic.
- A user can deliberately select an SDR BT.2020 target and VP builds the
  matching libplacebo target and DXGI request when the platform supports it.
- Unsupported presentation paths safely fall back without mis-signalling P2020
  values as P709.
- Source primaries, target primaries, transfer, range, and actual presentation
  result are separately visible in logs/OSD.
- Existing Rec.709 and HDR-to-SDR behavior is unchanged unless the user selects
  the new target/profile.
- The story is not Done until automated/color-readback evidence and real
  display/projector validation are recorded.
