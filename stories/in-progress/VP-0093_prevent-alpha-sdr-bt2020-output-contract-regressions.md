# VP-0093: Prevent Alpha SDR BT.2020 output-contract regressions

## Status

In Progress. Source branch: `codex/vp-0093-output-contract`, based on the
current `v1.1.015-beta` integration branch.

## Initial investigation (2026-08-05)

The regression is identified in the Alpha libplacebo renderer: later code
replaced VP-0064's proven F6 contract with a DXGI P2020-only transport and
suppressed the NVIDIA AVI InfoFrame override. That is explicitly incompatible
with this projector path. The repair restores the separate contracts:

- target pixels: SDR BT.2020, with the Rec.709-to-BT.2020 transform applied
  once by libplacebo;
- DXGI transport: the established P709/sRGB path; and
- HDMI signalling: NVIDIA AVI InfoFrame reporting when configured, including
  its existing set/readback/restore path.

The output policy now represents the SDR target separately from the transport,
with tests preventing a BT.2020 target from requesting P2020 DXGI or from
signalling BT.2020 for a Rec.709 target. `VideoProcessor-VPRenderer` x64
Release and the output-policy tests build and pass.

MPC Video Renderer review found a separate, broader source-metadata limitation:
VP currently represents primaries and YCbCr matrix through one `ColorSpace`
field, so BT.2020 non-constant and constant-luminance matrices cannot be
represented independently. This repair does not expand the metadata contract;
track that work separately with matrix/primaries/range/transfer golden tests.

## User story

As a projector user selecting the Alpha renderer's SDR BT.2020 display profile,
I want the picture, renderer target, transport choice, and HDMI signaling to
remain a verified coherent output contract, so pressing F6 never produces the
severely oversaturated appearance of BT.2020-target pixels being shown as
Rec.709.

## Regression report

Within the hour before this story was created, selecting the Alpha SDR BT.2020
profile again produced severe oversaturation. Manually changing the projector
between Auto, Rec.709, and BT.2020 did not correct the picture. The visual
failure resembles madVR producing BT.2020-target pixels while the projector is
interpreting them as Rec.709.

The currently available deployed `C:\Videoprocessor\vp\logs\vp_debug.log`
predates this incident and cannot establish its cause. Preserve it as baseline
context, but collect a fresh, timestamped reproducer log before drawing a
conclusion from it.

## Prior work and required preservation

- VP-0019 introduced SDR BT.2020 display profiles, F5/F6 selection, a real
  Rec.709-to-BT.2020 target transform, and optional NVIDIA AVI InfoFrame
  reporting.
- VP-0064 corrected a prior Alpha oversaturation issue by retaining the proven
  projector-compatible P709/sRGB DXGI transport while still rendering to the
  BT.2020 target and emitting verified BT.2020 AVI signaling. Its accepted
  implementation is `9eafcfe` on `v1.1.015-beta`.
- VP-0064's OSD intentionally distinguishes input colorspace, output target,
  requested transport, and actual transport. Do not regress that distinction
  by relabeling the transport as the target or vice versa.

The expected Alpha SDR BT.2020 path is therefore **not** simply a P2020
swapchain request: it is the proven target transform plus the compatible
presentation transport and, when enabled, readback-verified NVIDIA AVI
InfoFrame signaling.

## Scope

1. Reproduce the issue with a controlled F5 (Rec.709) -> F6 (BT.2020) -> F5
   sequence on Alpha, retaining the active deployed configuration and
   `VideoProcessor.state` as evidence before changing either one.
2. Compare the current source and deployed binaries/configuration/state to the
   accepted VP-0064 implementation. Identify every later change touching:
   - display-rule/profile selection and persistence;
   - target primaries, output policy, tone/gamut mapping, or 3D LUT setup;
   - DXGI swapchain colour-space/transport selection;
   - NVIDIA AVI InfoFrame save/set/readback/restore; and
   - Alpha renderer rebuild, fullscreen-host, and state-load ordering.
3. Instrument one bounded, generation-scoped **output-contract snapshot** at
   profile application, Alpha renderer initialization, first successful
   presentation, profile switch, and teardown. It must report at minimum:
   - selected display rule/profile and its origin (hotkey, configuration, or
     persisted state);
   - source EOTF/primaries/range and requested output target;
   - accepted target primaries and the selected colour-transform/matrix;
   - tone/gamut mapping and active LUT identity/contract, if any;
   - requested and actual DXGI transport colour space;
   - NVIDIA InfoFrame requested state, selected display ID, set result, and
     readback colourimetry/extended-colourimetry values;
   - renderer generation and whether the visible first frame belongs to that
     generation.
4. Add deterministic unit coverage for the output-policy and target-frame
   selection so a BT.2020 target cannot accidentally select the old P2020
   transport path, omit the Rec.709-to-BT.2020 transform, or apply it twice.
   Include Rec.709, SDR BT.2020, HDR/PQ, and no-profile cases.
5. Add a small, developer-only colour-reference diagnostic or GPU readback
   test that proves the intended primary conversion is applied exactly once at
   the target-frame boundary. It must not add a per-frame readback or affect
   release-frame pacing.
6. Verify the signal/transform contract on the actual NVIDIA/projector path
   with known Rec.709 and BT.2020 colour references, using Auto and the
   matching manual projector mode only as controlled validation cases.
7. Repair the identified regression with the smallest change. If full output
   contract verification is unavailable, preserve user-selected rendering but
   make the unverified component explicit in OSD/logs; do not silently claim a
   correct BT.2020 mode or silently reinterpret the selected target as Rec.709.

## Required diagnostic questions

- Did F6 select the intended `BT2020` display rule before Alpha initializes,
  or did a persisted/profile precedence path select a different rule?
- Did the target-frame transform convert into BT.2020 exactly once, and does
  any active LUT expect that same input primaries/transfer/range contract?
- Is the proven P709/sRGB transport still selected rather than the rejected
  P2020 swapchain path?
- Did NVIDIA InfoFrame reporting execute on the actual active output and does
  readback still show CTA-861 extended BT.2020 (`colorimetry=3`,
  `extended=6`) while F6 is active?
- Does a renderer rebuild, fullscreen transition, or persisted-state reload
  create a split state where pixels, transport, or signaling came from
  different profile generations?
- Can the observed failure be reproduced with no LUT and with the deployed
  LUT separately, to distinguish an output-contract regression from a LUT
  configuration contract issue?

## Explicit exclusions

- Do not change madVR rendering behavior; this story is Alpha-specific.
- Do not change general HDR tone-mapping algorithms, add P3 target support, or
  redesign LUT calibration. VP-0047 owns the P3-D65/LUT contract work.
- Do not add automatic display-mode/profile selection beyond the existing F5/F6
  and persisted-profile behavior.
- Do not overwrite the deployed configuration, projector settings, or
  `VideoProcessor.state` during diagnosis. Back up any file that must be
  changed for controlled validation.
- Do not use an OSD label alone as proof of output colour correctness.

## Verification

1. Fresh reproducer log, screenshot, and recorded configuration/state hashes
   for the failure, if reproducible.
2. F5 -> F6 -> F5 on Alpha in fullscreen, including a renderer restart after
   each state and an application restart with F6 persisted.
3. The same sequence with reporting enabled/disabled where the profile permits
   it, and with the active LUT disabled/enabled only for diagnosis.
4. Confirm normal SDR Rec.709, HDR input tone-mapped to SDR, and ordinary
   Alpha/madVR switching remain visually and diagnostically correct.
5. Validate on the actual projector in Auto plus its matching manual modes
   against reference patterns/content. Capture the output-contract snapshot
   for every validation run.
6. Run the x64 Release build and all relevant output-policy/renderer tests.

## Acceptance criteria

- The reproducer's root cause is identified as code, configuration/state,
  driver/display signaling, or LUT contract evidence—not guessed from the
  visible symptom.
- Alpha's active output-contract snapshot always identifies one generation and
  one coherent selected profile, target transform, LUT contract, transport,
  and signaling state.
- F6 produces the proven BT.2020-target/P709-transport/verified-AVI contract;
  F5 restores the Rec.709 target and prior InfoFrame state without stale
  BT.2020 processing or signaling.
- A transform, LUT, transport, or signaling step cannot be silently omitted,
  duplicated, or inherited from an earlier renderer generation.
- The repaired path passes deterministic tests and real projector validation
  without regressing Rec.709, HDR-to-SDR, profile persistence, or Alpha frame
  pacing.
