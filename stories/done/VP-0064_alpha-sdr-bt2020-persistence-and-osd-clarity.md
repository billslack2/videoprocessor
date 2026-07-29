# VP-0064: Persisted Alpha SDR BT.2020 output and clear OSD reporting

## Status

Done. Implemented on 2026-07-29 in commit `9eafcfe` on
`v1.1.015-beta`. The verified x64 Release build was deployed to
`C:\Videoprocessor\vp` and the developer confirmed that the picture and
colors are correct when selecting the SDR BT.2020 display profile with the
Alpha renderer.

## User story

As a user selecting SDR BT.2020 output, I want the Alpha renderer to preserve
the calibrated projector-compatible transport while signaling the intended
display target, and I want the OSD to explain the difference between input
metadata, rendered target, and transport details.

## Implemented behavior

- The SDR BT.2020 display profile continues to render toward a BT.2020 target
  and enables the NVIDIA BT.2020 AVI InfoFrame signaling path.
- The proven projector-compatible DXGI transport remains P709/sRGB rather
  than requesting a P2020 swapchain, avoiding the severe oversaturation seen
  with the latter combination.
- Persisted display-profile state is loaded before Alpha renderer
  initialization. The deployed state file records `profile.display: bt2020`
  when that profile is selected, so a restart restores the same signaling and
  target behavior.
- Profile-triggered Alpha rebuilds retain the valid fullscreen host instead of
  unnecessarily destroying and recreating it.
- OSD labels now distinguish `Input Colorspace`, `Output`, `Transport Req`,
  and `Transport Actual`, making it clear when BT.2020 is the selected target
  even though the DXGI transport is P709/sRGB.

## Validation

- x64 Release solution build completed successfully with the Visual Studio
  18 MSBuild toolchain.
- Deployed executable and Alpha renderer DLL hashes match the successful
  Release build outputs.
- Runtime log confirms persisted `display/bt2020` selection before Alpha
  initialization, SDR target `BT.2020`, and enabled NVIDIA BT.2020 AVI
  signaling.
- F5/Rec.709 behavior remains available through the existing display-profile
  selection path.
- User verification confirmed that the previously oversaturated image and
  incorrect visible colors were corrected; only the OSD wording needed this
  tracking record.

## Evidence

- Source commit: `9eafcfe` (`Fix Alpha SDR BT.2020 compatibility output`)
- Branch: `v1.1.015-beta`
- Deployment: `C:\Videoprocessor\vp`
- Runtime log: `C:\Videoprocessor\vp\logs\vp_debug.log`
- Persisted state: `C:\Videoprocessor\vp\VideoProcessor.state`
