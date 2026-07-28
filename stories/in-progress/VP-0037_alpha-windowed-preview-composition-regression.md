# VP-0037: Fix Alpha windowed-preview composition regression

## Status

In Progress — reopened on 2026-07-28 after live Release validation found that
Ctrl+I can leave Alpha video black or flashing while the OSD remains visible.
This violates the stable presentation acceptance criteria. The previous
Release deployment and child-preview contract evidence remain valid, but OSD
z-order/layering and renderer presentation interactions now require diagnosis
and a fix before acceptance.

Failed validation (2026-07-28): commit `11549c9` attempted to restart Alpha
between direct and composed contracts when Ctrl+I toggled the layered OSD.
Live testing caused severe full-screen flashing and temporarily starved normal
desktop/UI interaction. The deployed artifacts were immediately rolled back
to the prior Release build and verified; commit `66352c0` reverts the failed
source approach. The bad binaries are retained only for diagnosis at
`C:\Videoprocessor\vp\backup-bad-vp0037-osd-20260727-224150`.

Native OSD candidate (2026-07-28): after Microsoft DXGI guidance and three
independent code/graphics reviews, commit `f8d6d7d` replaces Alpha's visible
layered popup with a libplacebo `pl_overlay` blended into the final video
target. Ctrl+I performs no renderer restart, swap-chain recreation, HWND
z-order change, or output-contract transition. DirectShow/madVR retains the
legacy popup path. Release|x64 GUI and plugin builds passed and were deployed
with hash verification; the prior stable runtime is backed up at
`C:\Videoprocessor\vp\backup-before-vp0037-native-osd-20260727-225640`.
## User story

As an Alpha renderer user, I want video to remain visible and stable inside
VP's windowed preview, including while other applications are behind VP, and I
want switching between windowed and fullscreen modes to preserve the correct
presentation and color contract.

## Reported behavior

On the build identified in the title bar as commit `032935b`:

- Alpha reports `Rendering`, captures valid frames, and has no reported drops.
- The embedded video rectangle is white, transparent-looking, or empty.
- Content from a window behind VP can flicker through that rectangle.
- Fullscreen Alpha video works normally.
- The embedded preview worked before the recent output-presentation changes.

This is not currently reported for the established DirectShow renderer.

## Evidence from `C:\logs\vp_debug.log`

The session beginning `2026-07-27 21:54:54` shows a healthy capture/render
pipeline and a windowed flip-model swap chain:

```text
libplacebo settings: ... output_presentation=direct output_range=full ...
libplacebo DXGI swapchain: trigger=initialize ... windowed=1 model=FLIP/DIRECT-ELIGIBLE ...
libplacebo output negotiation (initialize): requested=DIRECT/FULL/AUTO/BT.2020 ...
Alpha presentation telemetry: ... source=3600 presented=3596 ... render_ms=0.29 swap_ms=1.64
```

There are no corresponding swap-chain creation, present, GPU, capture, or
render failures. Continuing presentation counters while the embedded rectangle
exposes lower-z-order content strongly indicate a Win32/DXGI composition
problem rather than missing source frames.

## Suspected regression boundary

Before Alpha output-contract work, swap-chain creation unconditionally used:

```cpp
swapchainParams.blit = true;
```

That selected the stable DWM-composed BitBlt path. Commit `acf685d` introduced
`output_presentation` and allowed `DIRECT` to select a flip-model swap chain.
Commit `83f63bf` subsequently required flip presentation for BT.2020, including
when a composed request would otherwise have been selected.

The deployed configuration contains:

```text
output_presentation: DIRECT
```

and the restored manual display profile selected BT.2020. The Alpha swap chain
is attached to VP's embedded `WindowedVideoWindow`, which is a child control
inside the MFC dialog. Fullscreen uses a separate top-level window and does not
reproduce the failure.

This is the leading diagnosis, not permission to assume every windowed
flip-model swap chain is invalid. Implementation must confirm the target HWND
style/ancestry and effective swap-chain behavior in logs.

## Required behavior

1. Alpha video must render visibly and without lower-z-order flicker in VP's
   embedded windowed preview.
2. The embedded preview must use a presentation path that is valid for its
   child HWND and desktop composition state. A user request for direct output
   must not force an unsafe child-window presentation model.
3. The preview's reported color contract must remain truthful:
   - if composed child-window presentation cannot faithfully carry the
     requested BT.2020 contract, use an explicit Full/sRGB/Rec.709 preview
     contract;
   - do not render BT.2020-encoded output while silently signaling Rec.709;
   - do not weaken or misreport the selected fullscreen output contract.
4. A top-level fullscreen render window may continue using the configured
   direct/flip and BT.2020 path when DXGI accepts it.
5. Switching windowed to fullscreen and back must create or reconfigure the
   swap chain for the new HWND class and output contract without stale
   resources, blank video, flicker, crashes, or restart loops.
6. Resizing, minimizing/restoring, occlusion, another window passing behind or
   over VP, display changes, renderer restart, and renderer switching must not
   expose unpainted or transparent preview content.
7. Existing refresh-rate switching, screen profiles, LUT validation, output
   range/gamma policy, queue behavior, and fullscreen presentation must not
   regress.

## Implementation guidance

1. Classify the presentation target from the actual HWND at renderer
   initialization and after target changes. At minimum inspect:
   - `GWL_STYLE` and `GWL_EXSTYLE`;
   - `WS_CHILD`;
   - parent/root/owner HWNDs;
   - visibility, iconic, and cloaked/occluded state where available;
   - client and screen rectangles;
   - target monitor.
2. Separate the **configured/requested output contract** from the
   **effective contract for this presentation target**. Do not mutate the
   user's configured fullscreen preference merely because the embedded preview
   needs a composed fallback.
3. For the embedded preview, prefer the previously proven composed path unless
   a tested child-window flip implementation is deliberately introduced.
4. If the preview falls back to Rec.709, build the libplacebo render target for
   that effective contract before rendering; changing only the DXGI color-space
   declaration is insufficient.
5. Reevaluate the target classification and effective contract when VP changes
   between the child preview and the fullscreen HWND.
6. Ensure the placeholder control paints black when no valid frame or swap
   chain is available so another application's pixels cannot appear through
   VP's client area.

## Required diagnostics

Add one concise target/presentation record at initialization, recreation,
resize/display change when relevant, and windowed/fullscreen transition. It
must include:

- trigger and renderer generation;
- render HWND, parent, root, and owner handles;
- style/ex-style and `is_child`;
- visible, iconic, and occlusion/cloak result when obtainable;
- client rectangle, screen rectangle, and monitor;
- requested presentation/range/gamma/primaries;
- effective presentation/range/gamma/primaries;
- swap effect, buffer count, format, windowed flag, color-space result, and
  fallback reason.

Example:

```text
Alpha presentation target: trigger=initialize hwnd=... parent=... root=...
is_child=1 visible=1 requested=DIRECT/FULL/AUTO/BT.2020
effective=COMPOSED/FULL/sRGB/REC.709 reason=embedded preview
```

When presentation succeeds but no image is visible, periodic telemetry should
make the discrepancy diagnosable without flooding the log. Include present
result, present count, last successful frame time, client size, target
visibility, swap-chain model, and effective contract. Log each state change,
not every frame.

## Verification

1. Reproduce with a browser or other visually distinct window behind VP and
   Alpha selected.
2. Verify stable video in the embedded preview for Rec.709 and BT.2020 display
   profile selections and for configured `AUTO`, `COMPOSED`, and `DIRECT`
   presentation requests.
3. Move, resize, minimize/restore, occlude, uncover, and change focus between
   VP and the background window. Confirm no background pixels appear in the
   video rectangle.
4. Repeatedly switch:
   - windowed preview to exclusive fullscreen and back;
   - windowed preview to windowed fullscreen and back, if supported;
   - Alpha to an established renderer and back.
5. Confirm fullscreen retains its requested and accepted direct/BT.2020
   contract while the embedded preview logs any intentional composed/Rec.709
   override.
6. Confirm frames continue presenting after each transition and that queue
   depth, drops, render time, swap time, scene detection, refresh selection,
   and LUT status remain healthy.
7. Run a clean build and retain a diagnostic log covering initialization,
   preview playback, fullscreen entry/exit, and final preview playback.

## Acceptance criteria

- Alpha windowed preview always displays video or an intentional black
  placeholder; it never exposes or flickers pixels from a lower-z-order
  window.
- Windowed/fullscreen transitions work repeatedly without manual renderer
  restart, crash, blank output, or resource leak.
- Requested and effective presentation/color contracts are distinct, truthful,
  and clearly logged.
- The embedded preview cannot force an unsafe direct child-window path merely
  because the configured profile requests `DIRECT` or BT.2020.
- Fullscreen direct/BT.2020 behavior remains available and does not regress.
- Diagnostic logs are sufficient to identify the HWND type, selected swap
  model, effective output contract, and reason for any fallback.

## Likely implementation areas

- `src/VideoProcessor-GUI/WindowedVideoWindow.*`
- `src/VideoProcessor-GUI/FullscreenVideoWindow.*`
- `src/VideoProcessor-GUI/VideoProcessorDlg.cpp`
- `src/VideoProcessor-Lib/libplacebo/LibplaceboOutputPolicy.*`
- `src/VideoProcessor-Lib/libplacebo/LibplaceboVideoRenderer.*`

## Readiness and dependencies

No separate product decision is required: a visible, stable embedded preview
is mandatory, and a false BT.2020 claim is not an acceptable workaround.
Before implementation, confirm the current default integration branch under
the story workflow and reproduce against that branch. If a composed child
preview cannot coexist with the current fullscreen target lifecycle, document
the exact API limitation and create a bounded spike rather than weakening the
acceptance criteria.
