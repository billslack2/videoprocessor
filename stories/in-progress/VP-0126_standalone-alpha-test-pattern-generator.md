# VP-0126: Standalone Alpha test-pattern generator

## Status

In Progress (2026-08-13). Readiness review completed against the current
`origin/v1.2.001-beta` integration branch at
`aa12e0a9440055e8fb8e45e5da6afa40cb72aef5`. The implementation uses the clean
worktree `C:\Users\bslac\Documents\Codex\2026-08-13\we\work\vp-pattern-generator`
on branch `VP0126-pattern-generator`. Normal capture and the regular VP operator
dialog are out of process scope while this mode runs.

Implementation commit: `c65b764`. The x64 Release solution builds, all five
focused calibration-pattern tests pass, and the standalone Config test suite
passes. A full native run passed 812 of 817 tests; the five failures are
configuration-reference/profile inventory tests outside the pattern-generator
files. Automated live-window verification remains limited because the Windows
control helper can enumerate but cannot activate this native MFC window.

An input-free runtime probe subsequently rendered the HDR cinema frame through
the actual Alpha D3D11/libplacebo plugin on the RTX 3080. The log proves native
BGRA ingress tagged PQ/BT.2020 with 0.005-1000-nit mastering metadata and
MaxCLL/MaxFALL 203, the configured `spline` tone mapper, a negotiated
`RGB_FULL_G22_NONE_P709` swapchain, and the final transition
`PQ (ST2084)/BT.2020 ... -> SDR Rec.709 100.0 nits`. The temporary probe switch
and test configuration were removed, the committed tree is clean, and the
normal x64 Release executable was rebuilt with all five focused tests passing.

Launcher/package follow-up commit: `877a17e`. Every x64 build now emits
`StartPatternGenerator.cmd` beside `VideoProcessorPatternGenerator.exe`, and
both are explicit release-manifest entries. The launcher sets its working
directory to the installation folder and starts only the dedicated executable.
The full x64 Release build and release-manifest dry run both pass with the two
artifacts included by default.

Global source/output follow-up commit: `1e1576a`. Two dropdowns now apply to
every pattern. Source selects either 8-bit full-range SDR Rec.709 or a real
HDR10 ingress contract: packed 10-bit legal-range R10l (codes 64-940), ST 2084
PQ, BT.2020 primaries, 0.005-1000-nit mastering metadata, MaxCLL/MaxFALL 203,
and 203-nit authored white. Output processing offers Automatic plus only the
display profiles parsed from the active VP configuration; the current example
configuration therefore exposes Rec709 and BT2020, with no generator-invented
mode and no persisted configuration mutation. The x64 Release generator builds
and all six focused calibration-pattern tests pass, including exact R10l row
layout, legal black, and PQ 203-nit white assertions.

Crash-path follow-up commit: `6f99576`. Windows Error Reporting recorded an
access violation in `mfc140u.dll` immediately after Alpha accepted the first
frame and before the first output-evidence dialog could begin. The dedicated
fullscreen path no longer opens that modal dialog over the active Alpha surface;
it presents the clean pattern immediately and records input/profile and output
contract evidence in the VP log instead. The x64 Release generator was rebuilt.

The implementation now builds a dedicated executable alias and feeds canonical
test frames directly to the real Alpha plugin. Alpha resolves the same unified
`VideoProcessor.cfg` (or explicit `--config`/`--vr_config`) as normal VP. The
pattern UI exposes no output-range, LUT, shader, scaling, viewport, gamma,
monitor-policy, tone-mapping, or presentation override.

VP-0127 synchronization note (2026-08-13): a live VP-0125 sweep proved that a
nonblack Alpha backbuffer and successful swap calls can coexist with a visually
black composed output. This generator must consume VP-0127's shared
render/submission/display-delivery result model. Until authoritative delivery
evidence exists, composed pattern presentation is explicitly `MEASURE` or
`DISPLAY DELIVERY UNVERIFIED`, not PASS or “visible.”

Synchronization implementation: commits `13c77be`, `d5cc6e3`, `bad105a`, and
`bb09e13` on
`origin/VP0126-pattern-generator` merges the complete VP-0125/VP-0127 output
path and its updated evidence documentation while retaining launcher-package
commit `877a17e`. Before the clean
pattern is exposed, the generator now shows requested/actual swapchain format,
renderer-readback state, submission state, DXGI delivery evidence, and the
DXGI declaration. It asks the tester to explicitly grade whether the intended
pattern is visible; a negative grade is logged and terminates that pattern as
a visible-delivery failure. The dialog reiterates that even presented-frame
evidence does not prove HDMI values or calibration accuracy.

The synchronized x64 Release GUI and VP-renderer projects build successfully.
The combined focused suite passed 44/44 tests: all 39 output-policy tests and
all five calibration-pattern tests.

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
   Ship a double-click command launcher beside the dedicated executable.
3. Show purpose and adjustment instructions before launching each pattern.
4. Present the chosen pattern borderlessly using normal VP fullscreen-monitor
   selection and fallback semantics. Any mouse click or ordinary key returns to
   the menu, except stepped sequences where it advances to the next frame and
   returns after the final frame.
5. Initial SDR patterns: brightness/PLUGE, white clipping/contrast, grayscale
   steps and gamma-identification ramps, RGB/CMY clipping, sharpness, geometry,
   and configurable full fields/windows where practical.
6. Reuse the actual Alpha presentation path. Pattern generation is isolated
   from capture, while Alpha intentionally applies exactly the configured VP
   tone mapping, LUTs, scaling, shaders, viewport/crop/NLS, and presentation.
7. Provide 15 individually colored, labeled cinema-aspect grids from 1.33:1
   through 2.76:1 as one stepped sequence. Global source and configured-output
   selectors apply to this sequence and every other pattern. HDR source peaks
   are 203 nits and include static source metadata so any configured VP
   HDR-to-SDR or HDR-output path can be exercised.
8. Surface VP-0127 presentation evidence for every displayed pattern:
   requested and actual swapchain format, renderer-content state, submission
   state, display-delivery state, and any fallback or format reconfiguration.

## Acceptance criteria

1. The explicit pattern-generator launch enters only the dedicated UI and no
   capture device is opened or enumerated for use.
2. The menu identifies the VP-selected configuration and target-monitor policy,
   explains every pattern before display, and is fully keyboard usable.
3. Every pattern fills the selected output at native raster size without
   window chrome, scaling blur, animation, or desktop content showing through.
4. A mouse click or keypress exits a single pattern. For a cinema-grid sequence,
   it advances through every ratio and returns to the menu after 2.76:1, without
   starting normal VP or leaving the output display mode altered.
5. Pattern code values and expected visible markers are unit-tested where the
   pattern can be represented independently of the GPU. The x64 Release build
   and relevant native/configuration tests pass.
6. SDR Rec.709 and HDR10 PQ/BT.2020 are source contracts, not generator-owned
   output modes or proof of physical wire values. VP configuration is the sole
   authority for output processing and presentation in both cases.
7. A generated pattern is never reported as visibly delivered solely from a
   nonblack renderer readback or successful swap call. Composed output without
   authoritative delivery evidence remains `DISPLAY DELIVERY UNVERIFIED` and
   offers an explicit tester grade.
8. The source and output selectors apply to every pattern. Output choices are
   restricted to Automatic and profiles that actually exist in the loaded VP
   renderer configuration; selection does not rewrite or persist configuration.

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
- VP-0127 owns authoritative format enforcement and the shared
  render/submission/display-delivery classification consumed by this story.
- Desktop composition, ICC/LUT state, driver range, and capture-card conversion
  can change measured values after application rendering. Diagnostics and docs
  must keep generated pixels, DXGI state, and measured HDMI/capture results
  distinct.
