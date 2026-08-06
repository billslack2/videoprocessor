# VP-0093: Prevent Alpha SDR BT.2020 output-contract regressions

## Status

Done (2026-08-05). The repair is merged and deployed; final projector use
confirmed the corrected BT.2020 picture path is practical for normal use.

## Review handoff (2026-08-05)

- Merged commits: `b5eaa0c` (InfoFrame hardening), `c00e913`
  (child-window fallback coverage), `2afa439` (live target switching), and
  `772c9c4` (retain the child-window swapchain while switching the target).
- `772c9c4` is present on both `origin/codex/vp-0093-output-contract` and
  `origin/v1.1.015-beta`.
- A forced x64 Release rebuild of `VideoProcessor-VPRenderer` succeeded and
  the resulting DLL was deployed without modifying the active configuration.
- The live-switch rejection was traced to comparing the embedded child
  window's composed swapchain to the raw direct-profile request. The final
  implementation preserves that existing swapchain and changes only the
  BT.2020 render target plus optional NVIDIA AVI InfoFrame.

Final review evidence remains the actual-projector F5 -> F6 -> F5 run. The
expected log is `libplacebo output target switched live` with
`swapchain_recreated=0`; the visible image should remain correct without an
Alpha renderer teardown.

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

### Reproduction and corrective implementation (2026-08-05)

Fresh Alpha F6 logs reproduced the wire-level defect. The selected
`display/bt2020` profile requested a direct `RGB_FULL_G22_NONE_P2020`
swapchain and DXGI accepted it, but the implementation explicitly logged
`NVIDIA override suppressed`; the Epson did not enter BT.2020 mode. This proves
that a successful `SetColorSpace1` call is not sufficient evidence for the
projector's HDMI signalling path.

Commit `b5eaa0c` restores the accepted VP-0019/VP-0064 contract on the current
integration base:

- F6 renders the SDR target in BT.2020 while retaining the proven P709/sRGB
  DXGI transport;
- it sends the NVIDIA AVI extended-colourimetry BT.2020 InfoFrame and restores
  the saved InfoFrame on F5 or renderer teardown; and
- it treats a successful one-shot NVAPI `SET` as physical signalling even when
  a subsequent `GET` returns the driver's automatic state. OSD distinguishes
  `verified`, `SET` (unverified readback), `display manual`, and `signal
  unavailable` rather than claiming that every request reached the projector.

The earlier clean x64 Release build at `b5eaa0c` had `VERSION_DIRTY=false` and
the complete test DLL passed 580/580 tests. The final merged renderer was then
forced through a clean x64 Release rebuild and deployed. Live validation must
use a controlled fullscreen F5 -> F6 -> F5 sequence on Alpha, checking both
the Epson mode and the generation-scoped log entries.

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

The fresh deployed `C:\Videoprocessor\vp\logs\vp_debug.log` now establishes
the cause: Alpha's P2020-only build completed DXGI negotiation while suppressing
the NVIDIA signalling path. Preserve that log as the failing baseline for the
controlled VP-0093 validation.

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
