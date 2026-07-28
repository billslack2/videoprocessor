# VP-0034: Restart-free mixed-aspect NLS

## Status

In Progress again as of 2026-07-27. The user requested that VP-0035's robust
low-latency active-aspect transition work be implemented as part of this story
on `codex/vp-0034-restart-free-nls` and draft PR
`billslack2/videoprocessor#15`, targeting the developer-confirmed default
branch `v1.1.014-beta`. Existing implementation commits are `4f4cc05` and
`a5c113d`.

Completed implementation:

- durable renderer-independent requested/effective NLS state and target;
- a stable output contract with restart-free nonlinear and linear-passthrough
  runtime mapping;
- renderer generations that invalidate old geometry while preserving the
  armed request and last safe mode;
- automatic request restoration and detector reacquisition after unrelated
  renderer replacement;
- distinct Active, Scope/linear passthrough, Waiting, and Off OSD states;
- mapping diagnostics including requested/effective rule, mapping mode,
  rectangle and active generation, source/target aspect, renderer generation,
  reason, and restart decision;
- screen-profile-specific output contracts: native-equivalent 16:9 for the
  Normal profile with side curtains closed, and 2.35 for the Scope profile;
- effective-aspect restart decisions, so native 16:9 and explicit 16:9 do not
  restart while a deliberate Normal/Scope contract change may restart once;
- matrix coverage confirming restart-free 4:3-to-16:9 NLS, 16:9-to-Scope NLS,
  1.90-to-Scope NLS, and 2.35 Scope passthrough;
- focused mapping and state-lifecycle tests plus updated configuration
  documentation.

Validation completed:

- Debug x64 GUI build passed;
- Release x64 full solution build passed;
- full Debug x64 native test suite passed, 130/130;
- focused Debug and Release x64 VP-0034 tests passed, 9/9 in each;
- `git diff --check` passed.

Current work: incorporate VP-0035's confidence-based transition model,
bounded detection latency, prompt generation consumption, and transition
diagnostics before returning the combined story to review.

This supersedes the assumption that the fixed-aspect completion of VP-0001
through VP-0003 is sufficient for variable-aspect movies. It does not reopen
those accepted stories.

## User story

As a Scope-screen viewer watching a movie that alternates between approximately
2.35:1 Scope and 1.85/1.90:1 IMAX scenes, I want one armed NLS selection to
follow every confirmed aspect change without restarting the renderer, losing
state, squashing the picture, retaining black bars, or failing to stretch the
IMAX scenes.

Marvel Studios' *Eternals* on Disney+ is the initial reported reproduction.
Fixed-aspect material such as sports already behaves correctly and must remain
correct.

## Reported behavior

- The result depends on whether NLS is enabled during a Scope or IMAX scene.
- Some Scope scenes become squashed while top and bottom bars remain.
- Some IMAX scenes are not stretched.
- The failure does not recover after waiting; it is not merely detector
  latency.
- Fixed-aspect 1.85/1.90-ish material can be stretched to Scope successfully.

These observations indicate a state/geometry transition failure rather than a
fundamental limitation of the nonlinear mapping.

## Current-code diagnosis

The current DirectShow NLS path separates the requested and effective shader
rules globally, but the renderer instance still owns the state needed for
future conditional reevaluation:

- `DirectShowGenericHDRVideoRenderer::SelectShaderRule` records the armed rule
  in `m_requestedShaderRule`.
- A change between the configured NLS output aspect and `nls_off` native output
  can request a renderer restart to renegotiate the media type.
- `MadVRShaderLoader` retains the effective runtime rule used while rebuilding
  the graph.
- A replacement `DirectShowGenericHDRVideoRenderer` does not restore
  `m_requestedShaderRule`, `m_inactiveShaderRule`, and associated applied
  geometry state.
- `RefreshShaderRule` immediately returns when `m_requestedShaderRule` is
  empty. Later Scope/IMAX transitions therefore stop being reevaluated.

This explains both the permanent failure and why the aspect visible when NLS
is selected changes the result.

The present configuration also gives `nls` a 2.35 output aspect while
`inactive_rule=nls_off` uses `output_aspect_ratio=native`. Treating every
confirmed scene-aspect transition as an output media-type change would require
repeated renderer restarts. That is unacceptable for variable-aspect content.

Relevant implementation areas include:

- `src/VideoProcessor-GUI/VideoProcessorDlg.cpp`
- `src/VideoProcessor-Lib/microsoft_directshow/MadVRShaderLoader.*`
- `src/VideoProcessor-Lib/microsoft_directshow/video_renderers/DirectShowGenericHDRVideoRenderer.*`
- `src/VideoProcessor-Lib/microsoft_directshow/live_source_filter/CBufferedLiveSourceVideoOutputPin.*`
- `Shaders/NLS.hlsl`

## Required behavior and design constraints

1. NLS has a durable renderer-independent **requested/armed state**. Replacing
   the renderer must restore that request and continue evaluating active
   geometry without requiring another shortcut press.
2. While NLS remains armed for a Scope target, confirmed content-aspect changes
   must not restart or rebuild the renderer, graph, live source, or queues.
3. Use one stable output/media-type contract while NLS is armed. Switch only
   the runtime geometry/mapping:
   - Scope content uses a linear Scope mapping that removes encoded letterbox
     bars and preserves the active picture without nonlinear distortion.
   - 1.85/1.90, 16:9, or other eligible content uses the existing nonlinear
     mapping from its detected active rectangle to the same Scope target.
4. Do not use the manual `nls_off`/native-output rule as the automatic
   per-scene fallback. Manual NLS Off remains a deliberate user command.
5. A manual initial transition into or out of an NLS output contract may use
   one controlled renderer restart if the current renderer interface makes
   media-type renegotiation unavoidable. No subsequent restart may be caused
   solely by Scope/IMAX scene changes while NLS remains armed.
6. Unrelated renderer restarts must restore the armed request, target screen
   profile, last safe effective mode, and enough state to resume automatic
   reevaluation after the detector reacquires geometry.
7. Runtime rule/geometry changes must be serialized safely with shader
   installation and renderer teardown. They must not drain VP or renderer
   queues, introduce a restart loop, or use geometry from a destroyed renderer
   generation.
8. Preserve fixed-aspect sports, 4:3-to-16:9 behavior from VP-0003, explicit
   manual NLS Off, and screen-profile switching.

## Observability

Log requested rule, effective mapping mode, active rectangle and generation,
source and target aspects, renderer generation, and the reason for every
mapping change. A normal scene transition should explicitly log a runtime
mapping change with `renderer_restart=0`.

The OSD should distinguish at least:

- `NLS: Active` for nonlinear stretch;
- `NLS: Scope passthrough` for linear Scope mapping;
- `NLS: Waiting` or `NLS: Transitioning` when geometry is not yet confirmed;
- `NLS: Off` for a deliberate manual off state.

## Verification

1. Start *Eternals* or equivalent deterministic test footage containing
   repeated `2.35 -> 1.90 -> 2.35` transitions.
2. Arm NLS once during a Scope scene and repeat the transition sequence.
3. Restart and arm NLS once during an IMAX scene, then repeat the same
   sequence.
4. Confirm Scope sections are geometrically correct with no residual encoded
   bars and no nonlinear distortion.
5. Confirm IMAX sections automatically stretch after stable detection.
6. Confirm no aspect transition produces renderer build/stop/start, graph
   reconnect, queue reset, HDR-mode flair caused by VP, queue starvation, or
   dropped frames.
7. Force one unrelated renderer restart while NLS is armed and confirm NLS
   resumes automatic mixed-aspect behavior without another shortcut press.
8. Verify manual NLS Off and fixed-aspect sports/4:3 control material.

Record logs covering requested/effective state, detector generations, runtime
mapping changes, renderer lifecycle, queue depths, and dropped frames.

## Acceptance criteria

- One NLS selection remains armed across all mixed-aspect scene changes and
  unrelated renderer rebuilds until the user explicitly turns it off.
- Confirmed Scope/IMAX transitions change mapping without restarting the
  renderer or graph.
- Scope content is neither squashed nor displayed with retained encoded bars.
- Eligible IMAX content is automatically stretched to the selected Scope
  target regardless of the aspect visible when NLS was enabled.
- Repeated transitions do not cause queue resets, starvation, dropped frames,
  or restart loops.
- Fixed-aspect NLS and manual-off behavior do not regress.

## Dependencies and relationship to other stories

- VP-0001 through VP-0003 provide the existing selection, conditional state,
  and active-rectangle foundation.
- VP-0035 separately improves transition responsiveness and false-positive
  resistance. VP-0034 must be correct with the existing conservative detector;
  detector speed must not be used to explain or mask permanent state loss.

