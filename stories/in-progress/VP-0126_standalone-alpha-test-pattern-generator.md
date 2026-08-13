# VP-0126: Standalone Alpha test-pattern generator

## Status

In Progress (2026-08-13). Readiness review completed against the current
`origin/v1.2.001-beta` integration branch at
`aa12e0a9440055e8fb8e45e5da6afa40cb72aef5`. The implementation uses the clean
worktree `C:\Users\bslac\Documents\Codex\2026-08-13\we\work\vp-pattern-generator`
on branch `VP0126-pattern-generator`. Normal capture and the regular VP operator
dialog are out of process scope while this mode runs.

The existing native Windows shell, fullscreen target selection, and Alpha/DXGI
presentation code provide the required platform pieces. The first implementation
slice is deliberately SDR-only; HDR patterns must not be advertised until VP can
apply and report a matching HDR swapchain/output contract and static metadata.

## User story

As a display owner, I want to launch VideoProcessor in a dedicated pattern
generator mode, read concise setup instructions, and show basic calibration
patterns through Alpha on a selected output, so I can adjust brightness,
contrast, grayscale/gamma tracking, color clipping, sharpness, and geometry
without starting capture or the normal VP runtime.

## Scope

1. Add an explicit special startup mode that is mutually exclusive with the
   normal VP application. It must not discover or open capture hardware and
   must not create the regular operator dialog.
2. Use a small native Win32/MFC menu; do not add Qt or a browser runtime.
3. Show purpose and adjustment instructions before launching each pattern.
4. Present the chosen pattern borderlessly on the selected monitor. Any mouse
   click or ordinary key returns to the menu; Escape also returns safely.
5. Initial SDR patterns: brightness/PLUGE, white clipping/contrast, grayscale
   steps and gamma-identification ramps, RGB/CMY clipping, sharpness, geometry,
   and configurable full fields/windows where practical.
6. Reuse the Alpha presentation path where it provides a truthful output
   contract. Keep deterministic pattern generation isolated from capture-frame
   ingestion and from content tone mapping, LUTs, scaling, shaders, crop/NLS,
   subtitles, and temporal processing.

## Acceptance criteria

1. The explicit pattern-generator launch enters only the dedicated UI and no
   capture device is opened or enumerated for use.
2. The menu identifies the target monitor and SDR output assumptions, explains
   every pattern before display, and is fully keyboard usable.
3. Every pattern fills the selected output at native raster size without
   window chrome, scaling blur, animation, or desktop content showing through.
4. A mouse click or keypress exits a displayed pattern back to the menu without
   exiting VP, starting normal VP, or leaving the output display mode altered.
5. Pattern code values and expected visible markers are unit-tested where the
   pattern can be represented independently of the GPU. The x64 Release build
   and relevant native/configuration tests pass.
6. SDR output is labelled as an application/DXGI contract, not proof of physical
   wire values. HDR remains unavailable until its colorspace, bit depth, PQ
   encoding, metadata, and capture/display validation are implemented together.

## Non-goals

- Capture, pass-through, normal VP playback, autocal, meter control, Dolby
  Vision, HLG, and network pattern-generator protocols.
- Claiming reference-generator accuracy without independent output/capture
  validation.
- Adding Qt, changing normal VP startup behavior, or exposing raw GPU/driver
  controls in the first slice.

## Dependencies and risks

- VP-0100/VP-0101 and VP-0125 contain broader pixel-owned output and VP-owned
  DXGI presentation work. This story may reuse safe presentation mechanisms but
  does not claim their unfinished physical-output proofs.
- Desktop composition, ICC/LUT state, driver range, and capture-card conversion
  can change measured values after application rendering. Diagnostics and docs
  must keep generated pixels, DXGI state, and measured HDMI/capture results
  distinct.
