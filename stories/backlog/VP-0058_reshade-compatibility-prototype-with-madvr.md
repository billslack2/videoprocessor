# VP-0058: ReShade compatibility prototype with madVR

## Status

Backlog spike. The first local experiment exposed a VP-specific madVR
window/input integration problem and was removed without changing VP source or
configuration. Further work requires a focused render-host prototype before
repeating the compatibility matrix.

This remains an experiment, not a committed renderer feature or distribution
change.

## Initial experiment result (2026-07-29)

The normal signed ReShade 6.7.3 build was installed with the DirectX
10/11/12 option. Although a dedicated test copy had been prepared, the
installer was inadvertently targeted at the active executable directory.
No `VideoProcessor.cfg`, renderer configuration, VP binary, source, or shipped
shader was modified. All ReShade-created files, downloaded effects, runtime
configuration, logs, backups, and the dedicated test-copy directory were sent
to the Windows Recycle Bin after the experiment. A post-removal scan found no
ReShade proxy DLL, runtime file, or shader directory in the deployment.

The experiment established the following:

- ReShade injected into `VideoProcessor.exe` and drew its startup banner over
  both DirectShow/madVR and Alpha output.
- Alpha's overlay was responsive and accepted normal keyboard input.
- With madVR, the overlay hotkey was not usable in VP's ordinary windowed
  layout. It became usable after entering VP fullscreen, but the overlay was
  extremely sluggish and keyboard keys behaved as though key-up state had
  been lost, causing continuing repeats.
- Assigning distinct overlay keys to the two ReShade runtime INIs and changing
  ReShade input processing from "block all input when the overlay is visible"
  to "pass on all input" did not fix the madVR lag or repeated-key behavior.
- During the failing madVR interval, VP conversion remained approximately
  2 ms per frame with normal capture/conversion queue progress. The visible
  lag was therefore downstream of VP capture and conversion.
- Matching VP and ReShade timestamps showed that a madVR startup created a
  D3D9Ex ReShade runtime and a D3D11/DXGI ReShade runtime against the same VP
  render target on different threads. The DXGI runtime was also destroyed and
  recreated across VP fullscreen/window transitions. ReShade reported an
  inconsistent DXGI adapter reference count during teardown.
- VP constructs exactly one selected `IVideoRenderer`; the D3D9Ex/DXGI pair
  observed during madVR playback is not an inactive Alpha renderer being kept
  alive. Alpha's implementation is a separate libplacebo D3D11 renderer.

This does not prove that ReShade and madVR are intrinsically incompatible.
ReShade is reported to work with madVR in MPC-family players. The result instead
points to VP's renderer-window ownership, focus, input routing, and fullscreen
lifecycle as the compatibility boundary.

## VP host-window findings

The current VP implementation has two different render hosts:

- windowed output uses `WindowedVideoWindow`, an MFC `CStatic` child;
- fullscreen output creates a separate top-level `FullscreenVideoWindow`;
- `DirectShowVideoRenderer::WindowSetup` gives the DirectShow renderer the
  selected host through `IVideoWindow::put_Owner`, applies
  `WS_CHILD | WS_CLIPSIBLINGS`, and sizes the renderer-owned child window;
- entering fullscreen schedules a delayed `SetForegroundWindow` and
  `SetFocus` on the fullscreen host rather than on the renderer-owned
  presentation child;
- VP's MFC dialog processes configured shortcuts through
  `PreTranslateMessage` and `TranslateAccelerator`;
- a fullscreen/window transition stops and reconstructs the renderer against
  a different host HWND instead of preserving one presentation-window
  identity.

That arrangement is consistent with the observed split behavior. In windowed
mode, keyboard focus remains in the dialog/control hierarchy while ReShade
associates input with a presentation swap chain. In fullscreen, focus reaches
the popup render host, so the overlay opens, but input is shared across the
host, renderer-owned child, MFC accelerator path, and two madVR graphics
runtimes. Reconstructing the graph/window also forces ReShade runtime teardown
and recreation.

## MPC-HC, MPC-BE, and mpv comparison

madVR exposes an explicit host-integration contract through
`IMadVRSubclassReplacement`. Its interface documentation says that madVR
normally subclasses an ancestor of its render window. A host that cannot
reliably tolerate that subclassing can call `DisableSubclassing()` and then
forward every message from the exact ancestor HWND through
`ParentWindowProc()`, honoring the returned handled/result values.

Both MPC-HC and MPC-BE implement that contract:

- they query `IMadVRSubclassReplacement` when creating madVR and call
  `DisableSubclassing()` before graph connection and window attachment;
- their top-level window procedures call `ParentWindowProc()` before normal
  application processing and return madVR's result when the message is
  handled;
- they configure both `IVideoWindow::put_Owner()` and
  `IVideoWindow::put_MessageDrain()`;
- MPC-HC keeps the graph alive during fullscreen transitions and switches the
  owner and message-drain HWND before destroying the previous fullscreen view;
- MPC-BE's fullscreen window forwards key-down, key-up, character, and system
  key messages exactly once to its main frame.

VP currently creates madVR through the generic DirectShow `CoCreateInstance`
path, uses `put_Owner()`, and has no use of `IMadVRSubclassReplacement`,
`DisableSubclassing()`, `ParentWindowProc()`, or `put_MessageDrain()`. This
allows madVR to subclass VP's MFC window hierarchy itself. The missing madVR
host contract is therefore the first compatibility defect to prototype; a
wholesale render-window redesign should not be the first change.

mpv is not a madVR host and is only supporting evidence for general Win32
window/input design. It preserves one HWND while changing fullscreen style and
position, handles key-down and key-up through one window procedure, and clears
held input on key-up and focus loss. Those patterns are relevant if the
madVR-specific fix does not fully resolve VP's fullscreen lifecycle problem.

## Backlog technical direction

Implement and validate in phases:

1. Add only the public `IMadVRSubclassReplacement` declaration needed by VP.
   Use a madVR-specific creation path rather than changing every DirectShow
   renderer.
2. Immediately after madVR `CoCreateInstance`, query and retain
   `IMadVRSubclassReplacement`, call `DisableSubclassing()` before graph
   connection or `put_Owner()`, and release the interface before releasing the
   renderer/filter.
3. Determine and log the exact ancestor HWND using the interface contract's
   documented algorithm. At the start of that HWND's window procedure, call
   `ParentWindowProc()` for every message and skip normal MFC processing when
   it returns `TRUE`.
4. Set `IVideoWindow::put_MessageDrain()` to one consistent VP command/input
   HWND. Do not also duplicate the same keyboard messages manually.
5. Run the minimal A/B test without changing VP's fullscreen architecture.
   ReShade's overlay must open in windowed and fullscreen modes, and releasing
   a key must stop it immediately.
6. Remove the delayed five-second `SetForegroundWindow`/`SetFocus` operation.
   Transfer focus synchronously only as part of a user-requested fullscreen
   transition. Log the focused, foreground, root-owner, host, and renderer
   HWNDs and their thread IDs at each transition.
7. If the minimal host-contract fix is insufficient, decouple fullscreen from
   graph/renderer reconstruction. Reassign madVR's owner and message-drain
   HWNDs while the graph remains live, and do not destroy the old host until
   madVR has been detached or reassigned. Confirm that borderless fullscreen
   no longer destroys and recreates the ReShade runtime.
8. Consider one persistent, focusable render-host HWND whose style and
   position change for fullscreen only as a larger follow-up.
9. Keep accelerator handling symmetric: do not synthesize duplicate keyboard
   messages; suppress repeat of one-shot VP commands only when key-down
   `lParam` bit 30 is set; never suppress `WM_KEYUP` or `WM_SYSKEYUP`.
10. Regress held letters and arrows, Alt, Home/Insert, focus loss, Alt-Tab,
    renderer switching, windowed/borderless/exclusive presentation, HDR,
    refresh switching, madVR, Alpha, NLS, renderer restart, and graph re-prime
    before repeating the full ReShade compatibility matrix.

Do not add ReShade detection, link an API, forward ReShade-specific commands,
or make VP responsible for ReShade configuration. The proposed work is a
madVR host-integration and renderer-host correctness improvement whose
compatibility effect can be measured independently.

## User story

As a VideoProcessor user who renders through madVR, I want to know whether
ReShade can safely apply a configurable final post-processing pass to the
madVR output, so I can evaluate its shader ecosystem without destabilizing
capture, timing, HDR treatment, or the existing renderer path.

## Background

ReShade is a generic post-processing injector. Its official site states that
it supports Direct3D 9/10/11/12, OpenGL, and Vulkan, and that effects are
written in ReShade FX. It is installed against an executable and operates by
intercepting the target process's graphics API calls.

When madVR is selected by VP, the relevant question is whether its final
presentation runs in the `VideoProcessor.exe` process and through an API that
ReShade can reliably intercept. If so, ReShade would be a post-renderer effect
chain: it would see madVR's composed output rather than VP's raw capture
frames, conversion buffers, metadata pipeline, queue, or frame timing.

This distinction is important. A successful prototype would not make ReShade
a VP shader implementation, and would not give VP ownership of ReShade's
effect settings, presets, performance, color pipeline, or lifecycle.

## Constraints

- Do not bundle, copy, redistribute, or automatically download ReShade
  binaries, shaders, preset packs, or add-ons. The official ReShade site says
  binaries and shader files must not be shared; users must obtain them from
  ReShade directly.
- Do not inject ReShade programmatically, modify its files, bypass its
  installer, alter its signing, or attempt to conceal it from security tools.
- Do not change VideoProcessor source, installer/release contents, default
  configuration, shader configuration, or help during this spike.
- Do not use the full-add-on build. Its official site describes that build as
  unsigned; it is outside this experiment.
- Do not evaluate anti-cheat compatibility. VP's capture/render use case is
  local video playback only.

## Prototype plan

1. Take a complete backup or restore point of the local test deployment,
   including its ReShade-related files if any. Keep `VideoProcessor.cfg` and
   `VideoProcessorRenderer.cfg` unchanged.
2. Use the normal ReShade installer from `https://reshade.me/` and target a
   dedicated test copy of `VideoProcessor.exe`, not the normal deployment.
   Select only the graphics API that the installer and observed madVR path
   identify; do not guess or install multiple proxy DLLs.
3. Begin with no third-party effects enabled. Confirm that the test process
   launches, madVR initializes, HDR/SDR and fullscreen/windowed presentation
   work, and ReShade's overlay can be opened without a VP crash or renderer
   restart loop.
4. Enable one lightweight, clearly visible final-pass effect (for example a
   simple color adjustment) solely to prove that ReShade sees the final madVR
   image. Record the selected API, DLL name/location, process ID, renderer
   mode, display mode, and effect timing.
5. Test a representative matrix: SDR 23.976, SDR 59.94, HDR10 input tone
   mapped by madVR, fullscreen/windowed presentation, renderer restart, and
   one display-refresh switch. Include an Alpha-renderer control case only to
   prove the experiment is restricted to madVR; do not pursue Alpha support in
   this story.
6. Measure visible correctness, madVR present/render timing, VP conversion
   time, VP and madVR queue health, capture misses, dropped/repeated frames,
   HDR/SDR output behavior, OSD, and stability. Remove ReShade and repeat the
   same cases as the baseline.
7. Remove all ReShade files from the dedicated test copy at the end of the
   spike unless the developer explicitly asks to retain that test environment.

## Questions to answer

- Which graphics API and process boundary does madVR actually use under VP?
- Does ReShade hook the final madVR presentation reliably in windowed,
  exclusive/fullscreen, and refresh-switch paths?
- Does the hook introduce queue starvation, elevated latency, dropped frames,
  mode-switch failures, stale frames, HDR/SDR signaling errors, or crashes?
- Does ReShade affect the final image after madVR tone mapping, and therefore
  require users to treat its effects as display-space adjustments rather than
  source-space processing?
- Can it coexist with VP's existing shader/NLS and renderer lifecycle without
  any VP code changes?
- What clear unsupported conditions should be documented if the prototype is
  viable (for example unsupported API, presentation mode, HDR behavior, or
  renderer choice)?

## Evidence and decision

Record the ReShade version, install/API selection, test executable location,
effect/preset name, renderer mode, source signal, display mode, measured
performance, VP log excerpts, and removal result. Do not commit ReShade
binaries, effects, presets, logs containing user paths, or screenshots with
private content to the repository.

Choose one outcome:

1. **Not compatible:** remove the test integration and document the concrete
   incompatible boundary.
2. **Compatible as an unsupported user experiment:** document only a short
   external setup note pointing users to ReShade's official installer, with
   explicit caveats and no VP code/release changes.
3. **Candidate optional integration:** create a separate design story covering
   support policy, configuration ownership, lifecycle, reset/rebuild behavior,
   color/HDR contract, diagnostics, security/signing implications, and a
   validation matrix. Do not promote this spike directly to implementation.

## Acceptance criteria

- The experiment establishes, from logs and observed presentation behavior,
  whether ReShade can or cannot hook final madVR output under VP.
- The result distinguishes final display-space post-processing from VP's
  source/capture processing.
- VP source, releases, default configuration, and shipped runtime contents are
  unchanged.
- ReShade is obtained and installed only by the tester from its official
  source; no ReShade content is redistributed by VP.
- The conclusion includes a reproducible setup/removal procedure and measured
  performance/stability evidence.

## References

- [ReShade official site](https://reshade.me/): supported graphics APIs,
  ReShade FX, installer workflow, and distribution restriction.
- [madVR `IMadVRSubclassReplacement` contract](https://github.com/clsid2/mpc-hc/blob/4d7c3dd6bdcd9ae87542f602b88a792aecebad73/include/mvrInterfaces.h#L218-L246)
- [MPC-HC disables madVR subclassing during creation](https://github.com/clsid2/mpc-hc/blob/4d7c3dd6bdcd9ae87542f602b88a792aecebad73/src/mpc-hc/FGFilter.cpp#L615-L625)
- [MPC-HC forwards host-window messages to madVR](https://github.com/clsid2/mpc-hc/blob/4d7c3dd6bdcd9ae87542f602b88a792aecebad73/src/mpc-hc/MainFrm.cpp#L22023-L22041)
- [MPC-HC configures owner and message-drain windows](https://github.com/clsid2/mpc-hc/blob/4d7c3dd6bdcd9ae87542f602b88a792aecebad73/src/mpc-hc/MainFrm.cpp#L15175-L15182)
- [MPC-HC switches fullscreen owner/message drain while the graph remains live](https://github.com/clsid2/mpc-hc/blob/4d7c3dd6bdcd9ae87542f602b88a792aecebad73/src/mpc-hc/MainFrm.cpp#L12390-L12434)
- [MPC-BE disables madVR subclassing during creation](https://github.com/Aleksoid1978/MPC-BE/blob/2f4b0f67eda0f3abfba448aeffade4537cda091b/src/apps/mplayerc/FGFilter.cpp#L504-L511)
- [MPC-BE forwards host-window messages to madVR](https://github.com/Aleksoid1978/MPC-BE/blob/2f4b0f67eda0f3abfba448aeffade4537cda091b/src/apps/mplayerc/MainFrm.cpp#L19375-L19385)
- [MPC-BE forwards fullscreen keyboard messages](https://github.com/Aleksoid1978/MPC-BE/blob/2f4b0f67eda0f3abfba448aeffade4537cda091b/src/apps/mplayerc/FullscreenWnd.cpp#L162-L192)
- [mpv preserves its HWND across fullscreen changes](https://github.com/mpv-player/mpv/blob/24c1cc52a36aa779010193a01d6ed13f902981f7/video/out/w32_common.c#L1074-L1138)
- [mpv handles Win32 key transitions](https://github.com/mpv-player/mpv/blob/24c1cc52a36aa779010193a01d6ed13f902981f7/video/out/w32_common.c#L468-L496)
- [mpv releases held input on key-up and focus loss](https://github.com/mpv-player/mpv/blob/24c1cc52a36aa779010193a01d6ed13f902981f7/video/out/w32_common.c#L1546-L1590)
- `src\VideoProcessor-Lib\microsoft_directshow\video_renderers\DirectShowVideoRenderer.*`
- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`: renderer lifecycle and
  handoff coordination.
