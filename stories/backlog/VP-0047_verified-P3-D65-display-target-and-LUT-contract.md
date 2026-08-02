# VP-0047: P3-D65 LUT-input and calibrated SDR output contract

## Status

Backlog. This work must not begin until the VP default integration branch is
discovered, the output-contract design is reviewed, and a user-approved source
branch is created from that default. It is a prerequisite for accepting a
calibration LUT authored for P3-D65 encoded RGB at the renderer output.

On 2026-08-02 this story absorbed VP-0048. Primaries, transfer, range, and
the distinction between the LUT-input and presentation/wire contracts are one
implementation boundary. VP-0048 is retained as a superseded record and must
not be implemented separately.

Design direction reviewed on 2026-08-02: P3-D65/gamma-2.2 is the verified
pre-LUT reference domain. The calibration cube then produces the projector's
expected native-output code values. VP must verify that internal LUT-input
domain and the separately supported post-LUT presentation contract; it must
not claim that generic Windows/DXGI output signals P3-D65 on the wire.

## User story

As a projector owner whose native gamut is calibrated with a P3-D65 LUT, I
want VP to convert content into a verified P3-D65 target contract before
applying my 3D LUT, so that a P3 gamma-2.2-to-native-gamut calibration is fed
the primaries, transfer, range, and reference levels it was authored to
receive.

## Current boundary and problem

`sdr_target_primaries` currently offers only `REC709` and `BT2020`. The
display LUT is correctly applied at the encoded target/native stage, after
VP's colour conversion, tone mapping, gamut mapping, and target transfer
encoding. Therefore a P3-D65 LUT cannot be safely used with a BT.2020 target:
P3 content contained in a BT.2020 signal is still represented by BT.2020 RGB
coordinates, not P3 RGB coordinates.

`lut_reference_primaries=p3_d65` is intentionally rejected today. VP has not
implemented the distinct P3-D65 LUT-input target, nor proved the values
immediately before the cube. Generic Windows/DXGI presentation has no direct
SDR P3-D65 declaration, so it cannot be used as evidence that a post-LUT
native-output signal is P3-D65.

The existing output policy also correctly rejects an explicit gamma-2.2
request: Full RGB G22 is the sRGB piecewise transfer, not a pure gamma 2.2
declaration, and the verified limited RGB path is gamma 2.4. Therefore
primaries cannot be added independently of transfer/range validation.

## Scope

1. Define one explicit target-contract model with separate but linked
   presentation/wire and LUT-input portions. Each names primaries, transfer,
   range, reference-white nits, black level, DXGI declaration, presentation
   mode, and evidence required for acceptance.
2. Add a first-class P3-D65 LUT-input target and only those SDR
   transfer/range combinations that the combined contract can prove. Surface
   the input and post-LUT presentation portions separately through the VP
   output policy, renderer settings, display rules, diagnostics, and Ctrl+I
   OSD.
3. Identify and verify the D3D11/DXGI presentation declaration(s) available
   for the post-LUT native-output path. Verify Check/Set/Check, present
   support, and restore behaviour as VP does for current Rec.709/BT.2020
   contracts; do not require or invent an SDR P3-D65 DXGI declaration.
4. Prove the P3-D65 domain at the LUT boundary with target-frame metadata and
   deterministic GPU readback. Separately establish whether the Windows
   compositor, GPU driver, and tested display path preserve the selected
   post-LUT presentation contract.
5. When both contracts are verified, render P3-D65 target RGB before the
   target-frame `PL_LUT_NATIVE` 3D LUT. Only then allow
   `lut_reference_primaries=p3_d65` to activate.
6. When VP cannot establish the P3 LUT-input or post-LUT presentation
   contract, reject the P3 profile and retain ordinary no-LUT playback; never
   reinterpret BT.2020 coordinates as P3.
7. Provide target-contract diagnostics that state requested versus accepted
   primaries, transfer, range, DXGI declaration, output mode, and concise LUT
   activation/rejection reason.
8. Add deterministic tests for output-policy selection, fallback/rejection,
   target-frame metadata, LUT-contract validation, profile switching, and
   restoration on teardown/device recreation. Add GPU/readback coverage for
   transfer/range endpoints and near-black behaviour where it can prove the
   LUT-input contract.
9. Validate on at least one real projector/display path with a known P3-D65
   test LUT and an independent test pattern measurement/reference.

## Non-goals

- Do not infer P3 solely from HDR mastering metadata, a BT.2020 container, or
  a display's advertised gamut.
- Do not use the LUT to silently perform the BT.2020-to-P3 conversion while
  declaring a P3 LUT contract.
- Do not signal, tag, or document the post-LUT native-output values as a
  generic P3-D65 wire signal merely because the LUT input is P3-D65.
- Do not relabel sRGB/G22 as a pure gamma 2.2 curve, or accept a calibration
  LUT for an unsupported transfer/range combination.
- Do not patch, replace, or upgrade bundled libplacebo as part of this story.
- Do not claim HDMI/DisplayPort signalling has changed unless it is verified
  on the selected driver/display path.
- Do not overwrite deployed configuration files or turn on a P3 profile by
  default.

## Design decisions required

- Which existing DXGI colour-space/format/presentation mode accurately models
  the post-LUT native-output contract for each supported projector path.
- The exact P3-D65/gamma-2.2 LUT-input transform and proof tolerance used for
  GPU readback, including the cube's expected input range and reference nits.
- Required projector native/calibrated picture mode, and the independent
  measurement evidence that validates it as an installation prerequisite.
- Whether optional vendor-specific display controls are worth a separate,
  non-portable future story. They are not required to claim P3 LUT-input
  support in this story.
- Exact semantics and compatibility rules for sRGB, BT.709/G22, gamma 2.4,
  BT.1886, gamma 2.2, and any bounded custom power-gamma LUT-input profile.
- Whether the public configuration uses named target-contract presets or
  validated expert fields, and migration behaviour for existing profiles.
- Required readback, metadata, and real-display evidence before the contract
  is marked verified.

## Acceptance criteria

- A P3-D65 profile activates only when VP proves both the P3-D65 LUT-input
  contract and the selected post-LUT presentation contract; otherwise it is
  clearly rejected before a LUT is applied.
- A P3-D65 3D LUT receives P3-D65 encoded RGB, not BT.2020 RGB whose content
  happens to be mostly P3.
- `lut_reference_primaries=p3_d65` activates only when the accepted target
  LUT-input contract is P3-D65; it reports a short, clear rejection otherwise.
- Every active LUT has one logged and OSD-visible resolved input contract:
  primaries, transfer, range, reference white nits, black level, and the
  accepted presentation/wire contract where it differs.
- Diagnostics explicitly identify P3-D65 as the LUT-input domain and do not
  claim a generic P3-D65 HDMI/DisplayPort signal after the LUT.
- Existing Rec.709 and BT.2020 profiles retain their current behaviour,
  including safe no-LUT fallback and colour-space restoration.
- Automated policy/contract tests and documented real-display validation pass.

## Dependencies and follow-up

VP-0048 has been consolidated into this story because explicit SDR
transfer/range handling and verified P3-D65 primaries are inseparable for a
calibration LUT. The prior references here to VP-0030 and VP-0031 were
incorrect: those IDs are debug-log stories, not colour-contract work.

## References

- Current target-LUT path:
  `src\\VideoProcessor-Lib\\vprenderer\\LibplaceboVideoRenderer.cpp`.
- Current output policy:
  `src\\VideoProcessor-Lib\\vprenderer\\LibplaceboOutputPolicy.cpp`.
- Current user-facing renderer documentation: `CONFIGURATION.html`.
- Current integration baseline: `origin/v1.1.015-beta` at `327ca7f`.
- Comparative Windows paths: mpv's D3D11 output exposes sRGB, linear, PQ,
  and BT.2020, while MPC Video Renderer uses P3 internally but presents HDR
  through PQ/BT.2020; neither is a generic SDR P3-D65 presentation contract.
