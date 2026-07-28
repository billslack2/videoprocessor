# VP-0030: Verified P3-D65 display target and LUT contract

## Status

Backlog. This work must not begin until the VP default integration branch is
discovered, the output-contract design is reviewed, and a user-approved source
branch is created from that default. It is a prerequisite for accepting a
calibration LUT authored for P3-D65 encoded RGB at the renderer output.

## User story

As a projector owner whose native gamut is calibrated with a P3-D65 LUT, I
want VP to convert content into verified P3-D65 target RGB before applying my
3D LUT, so that a P3 gamma-2.2-to-native-gamut calibration is fed the colour
coordinates it was authored to receive.

## Current boundary and problem

`sdr_target_primaries` currently offers only `REC709` and `BT2020`. The
display LUT is correctly applied at the encoded target/native stage, after
VP's colour conversion, tone mapping, gamut mapping, and target transfer
encoding. Therefore a P3-D65 LUT cannot be safely used with a BT.2020 target:
P3 content contained in a BT.2020 signal is still represented by BT.2020 RGB
coordinates, not P3 RGB coordinates.

`lut_reference_primaries=p3_d65` is intentionally rejected today. VP has not
implemented or proved a Windows/DXGI presentation contract that both renders
P3-D65 target values and accurately describes what the display receives.

## Scope

1. Add a first-class P3-D65 output-target request to the VP output policy,
   renderer settings, display rules, diagnostics, and Ctrl+I OSD.
2. Identify the exact D3D11/DXGI colour-space declaration(s) available for the
   relevant display connection modes, and verify Check/Set/Check, present
   support, and restore behaviour as VP does for current Rec.709/BT.2020
   contracts.
3. Establish whether the Windows compositor, GPU driver, and tested display
   path preserve that declaration. The implementation must distinguish a
   verified P3 signal from an application-local P3 render target that cannot
   be relied upon at the wire/display boundary.
4. When the P3 target is verified, render P3-D65 target RGB before the
   target-frame `PL_LUT_NATIVE` 3D LUT. Only then allow
   `lut_reference_primaries=p3_d65` to activate.
5. When P3 cannot be verified, reject the P3-target profile and retain
   ordinary no-LUT playback; never reinterpret BT.2020 coordinates as P3.
6. Provide target-contract diagnostics that state requested versus accepted
   primaries, transfer, range, DXGI declaration, output mode, and concise LUT
   activation/rejection reason.
7. Add deterministic tests for output-policy selection, fallback/rejection,
   target-frame metadata, LUT-contract validation, profile switching, and
   restoration on teardown/device recreation.
8. Validate on at least one real projector/display path with a known P3-D65
   test LUT and an independent test pattern measurement/reference.

## Non-goals

- Do not infer P3 solely from HDR mastering metadata, a BT.2020 container, or
  a display's advertised gamut.
- Do not use the LUT to silently perform the BT.2020-to-P3 conversion while
  declaring a P3 LUT contract.
- Do not patch, replace, or upgrade bundled libplacebo as part of this story.
- Do not claim HDMI/DisplayPort signalling has changed unless it is verified
  on the selected driver/display path.
- Do not overwrite deployed configuration files or turn on a P3 profile by
  default.

## Design decisions required

- Which DXGI colour-space/format/presentation modes, if any, accurately model
  full and limited P3-D65 output for the supported GPU/Windows paths.
- Whether P3 output is only supportable in a documented subset of modes, and
  the exact user-visible rejection/fallback behaviour elsewhere.
- Relationship between P3 output metadata and optional GPU/display signalling.
- Whether VP treats the projector's selected native-gamut mode as an external
  installation prerequisite rather than attempting to signal that mode.
- Required readback, metadata, and real-display evidence before the contract
  is marked verified.

## Acceptance criteria

- A P3-D65 profile either establishes a proven end-to-end output contract or
  is clearly rejected before a LUT is applied.
- A P3-D65 3D LUT receives P3-D65 encoded RGB, not BT.2020 RGB whose content
  happens to be mostly P3.
- `lut_reference_primaries=p3_d65` activates only when the accepted target
  contract is P3-D65; it reports a short, clear rejection otherwise.
- Existing Rec.709 and BT.2020 profiles retain their current behaviour,
  including safe no-LUT fallback and colour-space restoration.
- Automated policy/contract tests and documented real-display validation pass.

## Dependencies and follow-up

VP-0031 defines explicit SDR transfer/range profiles needed by common P3
gamma-2.2 calibration LUTs. The two stories should share one contract model,
but VP-0030 may land verified P3 primaries independently if a supported output
transfer is already available.

## References

- Current target-LUT path:
  `src\\VideoProcessor-Lib\\libplacebo\\LibplaceboVideoRenderer.cpp`.
- Current output policy:
  `src\\VideoProcessor-Lib\\libplacebo\\LibplaceboOutputPolicy.cpp`.
- Current user-facing renderer documentation: `VideoProcessorRenderer.html`.
- Current safety baseline: `ccc3c06` on `VP0011+0012`.
