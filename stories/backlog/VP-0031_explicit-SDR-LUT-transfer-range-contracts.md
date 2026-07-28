# VP-0031: Explicit SDR LUT transfer and range contracts

## Status

Backlog. This is design and implementation work for correctly delivering the
encoded SDR values a display-calibration LUT was authored to receive. It must
not begin until the VP default integration branch is discovered and a
user-approved source branch is created from it.

## User story

As a calibrated-display user, I want to select an explicit SDR target transfer
and range for each display profile—such as Rec.709 BT.1886, Rec.709 gamma 2.4,
or P3-D65 gamma 2.2—so that the matching 3D LUT can convert that intentional
input contract to my display's native gamut without an undocumented transfer
or range mismatch.

## Current boundary and problem

VP currently applies a display LUT after target transfer encoding. It validates
the declared `lut_reference_primaries`, `lut_reference_transfer`,
`lut_reference_range`, and reference nits against the accepted target before
activating the LUT. This prevents wrong-domain application but exposes the
current Windows/DXGI limits:

- Full RGB G22 is the sRGB piecewise transfer; it is not an arbitrary pure
  gamma 2.2 declaration.
- The verified limited RGB output contract is gamma 2.4.
- Some values accepted by broad configuration parsing do not yet have a
  verified matching presentation declaration and must not be treated as a
  valid LUT-input contract.

As a result, a LUT authored for Rec.709/P3 gamma 2.2 or a user-selected
Rec.709 2.4/BT.1886 target cannot be assumed to receive the values in its
authoring specification.

This story must distinguish two related but different contracts:

1. **Presentation/wire contract**: the DXGI/Windows/GPU declaration VP can
   truthfully request, verify, and (where applicable) signal to the display.
2. **LUT-input contract**: the precise encoded RGB domain VP generates
   immediately before the calibration LUT: primaries, transfer, range,
   reference white, and black level.

The two may be identical for standard paths, but they must not be conflated.
For example, DXGI Full G22 is sRGB's piecewise transfer, not proof that the
LUT receives an arbitrary pure gamma 2.2 domain. Conversely, a calibrated
native-gamut projector workflow may need a documented LUT-input domain even
when the display is manually placed in its appropriate picture mode and the
wire declaration cannot express that calibration domain directly.

## Scope

1. Define a single explicit target-contract model with separate presentation
   and LUT-input portions. It must cover primaries, transfer, range, reference
   white nits, black level, DXGI declaration, presentation mode, and the proof
   linking the generated LUT-input values to the declared model. Replace
   ambiguous independent settings where a combination is invalid with
   validated profile choices or equally clear compatibility rules.
2. Inventory every target transfer VP can truthfully generate and signal on
   the supported D3D11/DXGI paths. At minimum distinguish sRGB, BT.709/G22,
   gamma 2.4, BT.1886 where representable, and arbitrary/power gamma values
   that cannot be established as a wire contract.
3. Add only presentation and LUT-input profile combinations whose respective
   claims can be proved using DXGI capability negotiation, target-frame
   metadata, deterministic render/readback evidence, and repeatable output
   validation. An unsupported presentation declaration must not be invented;
   an unproven LUT-input domain must not be accepted merely because the user
   selected a compatible projector mode. Unsupported combinations must be
   rejected at profile load or activation with a concise explanation.
4. Ensure the target-frame LUT is applied after the selected colour conversion
   and target transfer encoding, with no hidden second conversion after it.
5. Make `lut_reference_*` validation resolve against the accepted target
   contract, not merely requested configuration. Expose the resolved contract
   in Ctrl+I and in a single clear log line.
6. Add named sample profile documentation for the supported cases, including
   authoring guidance for 709 BT.1886/2.4, 709 sRGB/G22, BT.2020, and any P3
   contract added by VP-0030. Do not ship or enable a user's calibration LUT.
   Include gamma 2.2, gamma 2.3, gamma 2.4, and a bounded custom power-gamma
   workflow only where VP can prove the requested LUT-input domain. Document
   BT.1886's authoring black and white values explicitly.
7. Add unit tests for valid/invalid cross-products, capability fallback,
   LUT activation/rejection, and restoration after display-rule switches.
   Add deterministic GPU/readback tests for transfer/range endpoint and
   near-black behaviour where those tests can prove the target contract.

## Non-goals

- Do not relabel sRGB/G22 as a pure gamma 2.2 curve.
- Do not silently approximate an unsupported target curve and then accept a
  LUT authored for a different curve.
- Do not use a manually selected projector/native-gamut mode as proof that
  VP generated the requested LUT-input primaries, transfer, range, or levels.
- Do not move or alter the user's LUT generation/calibration workflow beyond
  documenting VP's verified input contracts.
- Do not override display/projector picture mode, EDID, GPU control-panel
  settings, or deployed configuration without explicit user approval.
- Do not change libplacebo itself.

## Design decisions required

- Whether the public configuration remains composable fields or becomes named
  target-contract presets plus expert overrides.
- Exact semantics and documentation for `BT1886`, `2.2`, `2.4`, `sRGB`, and
  `AUTO`, plus gamma 2.3 and bounded custom power gamma, including whether
  each value means an encoder transform, a DXGI declaration, a LUT authoring
  assertion, or some combination.
- Exact separation, compatibility rules, and diagnostics for presentation/wire
  and LUT-input contracts. A display's manually selected native-gamut mode may
  be an installation prerequisite, but cannot substitute for either proof.
- The required evidence for accepting a full-range or limited-range target as
  a LUT input contract on each presentation path.
- How reference nits interact with SDR transfer selection and LUT contract
  matching, particularly for BT.1886 calibration workflows. Specify both the
  LUT authoring white/reference level and black level where BT.1886 or a
  custom display-referred curve depends on them.
- Versioning/migration for existing settings and the safe behaviour of legacy
  profiles that request an unsupported combination.

## Acceptance criteria

- Every active LUT has one logged and OSD-visible resolved input contract:
  primaries, transfer, range, reference white nits, and black level; it also
  identifies the accepted presentation/wire contract where that differs.
- VP accepts a calibration LUT only when the actual target matches its declared
  contract; otherwise it keeps normal playback and explains the rejection.
- The documentation distinguishes sRGB/G22, BT.709/G22, gamma 2.4, BT.1886,
  and unsupported pure-power gamma targets without conflating them.
- New supported profile combinations have automated output-policy and
  LUT-contract coverage plus real-output evidence appropriate to the claim.
- Existing profiles maintain current output behaviour unless the user selects
  a new validated target profile.

## Dependencies and coordination

VP-0030 consumes this model for P3-D65 gamma-2.2 calibration. Implement the
shared contract representation once; do not create separate primaries and
transfer validation paths that can drift apart.

## References

- Current output policy:
  `src\\VideoProcessor-Lib\\libplacebo\\LibplaceboOutputPolicy.cpp`.
- Current display LUT validation:
  `src\\VideoProcessor-Lib\\libplacebo\\LibplaceboDisplayLut.cpp`.
- Current renderer documentation: `VideoProcessorRenderer.html`.
- Current safety baseline: `ccc3c06` on `VP0011+0012`.
