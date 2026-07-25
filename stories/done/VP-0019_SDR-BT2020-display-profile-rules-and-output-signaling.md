# VP-0019: SDR BT.2020 display profiles, F5/F6 hotkeys, and output signaling

## Status

Done — accepted and deliberately released from `v1.1.014-beta` on
2026-07-25.

The x64 Release solution build succeeded and was deployed to
`C:\videoprocessor\vp`. The implementation was committed and published on
`v1.1.014-beta` as
`6b89cf6add939567e777331a17d6e13f8f8bafa3`. Release artifact reference:
`VideoProcessorLibplacebo.dll` SHA-256
`A9BAA450E4E62624FC54102817F34E664EE69600392D96D807C04CD87C0437DC`.

Hardware validation on the NVIDIA/projector chain confirmed that F6 renders
BT.2020 and switches the projector to BT.2020 mode, while F5 renders Rec.709
and removes the BT.2020 indication. The runtime log verified AVI InfoFrame
`colorimetry=3`, `extended=6` after F6 and successful restoration to the
captured `0/0` state after F5. Repeated switching and renderer reconstruction
also succeeded.

Deployment configuration uses `F5` for the Rec.709 profile and `F6` for the
BT.2020 profile.  Checked-in default configuration documents those bindings but
does not enable either deployment-specific profile/binding.

## Context

VP currently has source/container color-space handling, including `ColorSpace::BT_2020`, but its SDR output target is Rec.709.  VP-0004 explicitly leaves true SDR BT.2020 output as follow-up work.

That is distinct from VP-0009.  VP-0009 makes the alpha renderer accept additional DeckLink source pixel formats and preserves their source conversion; it does not give a display profile a BT.2020 target or signal that target to the display.

madVR exposes the user-facing behavior needed here: a BT.2020-calibrated display profile can request that BT.2020 be reported to the display.  The request must be withdrawn when a Rec.709 profile becomes effective; otherwise Rec.709 material could be interpreted using BT.2020 primaries.

NVIDIA NVAPI exposes HDMI AVI InfoFrame control independently from the DXGI
swap-chain color space. madVR identifies `NvAPI_Disp_InfoFrameControl` as the
API it uses for this feature. VP must use that API for the requested
madVR-equivalent "report BT.2020 to display" behavior, verify the effective
state, and restore the exact prior NVIDIA color state when the profile changes.
Output rendering must remain independent: a failed or disabled signaling
request does not change a BT.2020 profile back to Rec.709 pixels.

## User story

As a user with separately calibrated Rec.709 and BT.2020 display modes, I want F5 to select the Rec.709 profile and F6 to select the BT.2020 profile, and have VP signal BT.2020 only while the F6 profile is active, so my display/projector uses the correct gamut and returns to Rec.709 when I press F5.

## Scope

1. Define an explicit **SDR output target primaries** setting owned by a display profile:
   - `REC709` (default, preserving current behavior);
   - `BT2020`.
   It is a display-target setting, not an instruction to trust or override source primaries.
2. Make the profile system select the Rec.709 profile with F5 and the BT.2020 profile with F6.  The active profile, hotkey action, and fallback must be deterministic and visible in logs/OSD.  Checked-in configuration documents these deployment bindings but leaves them disabled; deployed configuration enables them.  No automatic content, input, refresh-rate, or display-mode matching is required.
3. Have the selected profile drive the renderer's color-management target.  For SDR BT.2020, convert source RGB into BT.2020 display primaries; do not merely relabel Rec.709 pixels as BT.2020.
4. Add a separate per-profile opt-in setting, for example `report_bt2020_to_display`.  It is valid only when the effective SDR output target is `BT2020` and must default to off.
5. When the opt-in is active, use `NvAPI_Disp_InfoFrameControl` on the active NVIDIA display to save the current HDMI AVI InfoFrame, set extended BT.2020 colorimetry, and read it back for verification.
6. On every transition away from an effective BT.2020-reporting profile - especially the F5 Rec.709 selection, as well as renderer recreation, display/refresh switch, fullscreen/window transition, source stop, or failed setup - restore the HDMI AVI InfoFrame saved before F6. Never leave stale BT.2020 signaling behind.
7. Expose requested target primaries, reporting request, NVAPI set/verification result, and restoration result in the log and OSD/diagnostics.

## Non-goals

- Do not change the source/container color-space override semantics.
- Do not convert SDR Rec.709 content to BT.2020 merely because the source declares BT.2020; the display profile determines the target.
- Do not add HDR metadata, PQ, HLG, or Windows HDR-mode management; this story is SDR BT.2020 signaling only.
- Do not require NVIDIA signaling in order to render BT.2020. BT.2020 reporting is NVIDIA-only and documented for cautious use; unsupported hardware or an NVAPI failure must retain the selected BT.2020 render target with a clear diagnostic.
- Do not fold R210/R12B source-format conversion from VP-0009 into this story.
- Do not add automatic profile rules or content detection.

## Implementation plan

1. Map the existing profile configuration and hotkey-selection flow, including active-profile calculation, F5/F6 action, and renderer-restart/reconfiguration boundaries.  Add target primaries and reporting fields to the persisted profile schema with backwards-compatible defaults.  Keep the checked-in bindings disabled but documented, and enable them only in deployment configuration.
2. Add validation when loading/applying a profile:
   - `report_bt2020_to_display=true` with target `REC709` is rejected or normalized to false with a prominent configuration error;
   - unknown primary values are rejected;
   - unavailable NVIDIA InfoFrame signaling never changes the renderer's actual
     color transform or silently claims that BT.2020 reporting succeeded.
3. Thread the effective display target into every SDR renderer path, including alpha, so its final color transform is Rec.709-to-BT.2020 (or appropriate source-to-target conversion) before presentation.
4. Encapsulate NVIDIA output signaling. Map the active Windows display name to
   an NVAPI display ID, save its current `NV_INFOFRAME_DATA`, set the AVI
   colorimetry field to extended and its extended-colorimetry field to the
   CTA-861 BT.2020 value, and verify with a readback.
5. Keep signaling transactional and independent from rendering. If setup or
   verification fails, log the failure and continue with the selected BT.2020
   render target. On F5 or teardown, restore the exact saved NVIDIA state.
6. Add deterministic tests for F5/F6 profile persistence, target-transform
   selection, NVAPI success/failure, and every BT.2020-to-Rec.709 restoration
   transition.

## Verification

- Completed on the deployed NVIDIA/projector chain on 2026-07-25.
- Release x64 solution build succeeded.
- F6 log evidence:
  `AVI InfoFrame enabled ... verified_colorimetry=3 verified_extended=6`,
  followed by `SDR target request=BT.2020`.
- F5 log evidence:
  `AVI InfoFrame restore ... colorimetry=0 extended=0 result=NVAPI_OK`,
  followed by `SDR target request=Rec.709`.
- The projector visibly entered and exited BT.2020 mode as expected.

## Acceptance criteria

- A profile can explicitly target Rec.709 or BT.2020 primaries, with Rec.709 as the compatible default.
- F5 reliably activates Rec.709, F6 reliably activates BT.2020, and the effective profile is observable.
- BT.2020 reporting is opt-in per BT.2020 profile, NVIDIA-only, and only declared successful after NVAPI readback verifies it.
- Any switch to Rec.709 - or any teardown/failure path - restores the saved NVIDIA output state.
- Pixel conversion follows the selected target independently of optional display signaling; BT.2020 rendering continues when signaling is disabled or unavailable.
- The feature works in the alpha renderer and does not regress the established SDR renderer paths.
