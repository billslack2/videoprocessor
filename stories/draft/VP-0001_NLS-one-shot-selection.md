# VP-0001: Make manual NLS activation one-shot and queue-safe

## Status

Draft.

## Scope and repository context

This story applies to the DirectShow external-shader renderer path used with
madVR. The source repository is:

`C:\Users\bslac\vp\videoprocessor - VS2026`

The deployed configuration used for reproduction is:

`C:\Videoprocessor\vp\VideoProcessor.cfg`

The relevant current configuration is `[shaders.nls]`, selected with
`shortcut=N` (Shift+N). `[shaders.nls_off]` uses `shortcut=n` (plain N). The
shortcut parser intentionally treats uppercase letter keys as Shift-modified.

Do not change the capture queue architecture or madVR settings as part of this
story. This is a shader-command idempotency and renderer-rebuild issue.

## User story

As a viewer, when I select NLS once, VP should apply it once and perform at
most one controlled renderer restart. Repeated keyboard auto-repeat or command
delivery must not repeatedly clear madVR's shader chain, drain its queues, or
cause dropped frames.

## Reproduction and evidence

Use `C:\logs\vp_debug.log` from 2026-07-23.

At `23:22:59`, one NLS activation resulted in 37 pairs of messages in roughly
two seconds:

```text
Shaders: runtime selection changed to "nls"
Shader rule changed to 'Nonlinear Stretch'
```

Each request calls `MadVRShaderLoader::ApplyConfiguredShaderRule`, which calls
`ApplyConfiguredShaders`. That function clears both external shader stages and
reinstalls the shader chain. The conversion worker remained healthy afterwards
(`ConvertedQueue=8`, `BackpressureHits=0`), so capture/conversion was not the
cause of the observed madVR queue drain.

## Relevant implementation points

- `src/VideoProcessor-GUI/VideoProcessorDlg.cpp`
  - `CreateConfiguredAccelerators` parses dynamic shader-rule shortcuts.
  - `OnCommandShaderRule` receives accelerator commands, calls
    `SelectShaderRule`, and requests a renderer restart when needed.
  - `UpdateState` executes a pending renderer restart.
- `src/VideoProcessor-Lib/microsoft_directshow/video_renderers/DirectShowGenericHDRVideoRenderer.cpp`
  - `SelectShaderRule` currently applies every received selection.
- `src/VideoProcessor-Lib/microsoft_directshow/MadVRShaderLoader.cpp`
  - `ApplyConfiguredShaderRule` updates runtime selection and applies shaders.
  - `ApplyShaderEntries` clears both madVR external shader stages before
    adding shaders again.

## Implementation plan

1. Add a monotonically increasing shader-request ID and source to selection
   logging. Sources must distinguish `shortcut`, UI, automatic aspect refresh,
   and renderer rebuild.
2. Make a manual selection idempotent. If the same rule is already requested,
   effective, or pending a rebuild, return success without clearing, compiling,
   or reinstalling external shaders.
3. Coalesce repeated accelerator messages while a shader aspect-ratio restart
   is pending. A held key may produce many Windows accelerator events but must
   produce one state transition.
4. When the new rule changes negotiated media aspect, record the request and
   defer final shader installation to the replacement renderer after rebuild.
   Do not keep mutating the old madVR instance while its replacement is pending.
5. Keep automatic aspect refresh separate from manual selection. It may update
   effective geometry, but must not replay a user request or allocate a new
   request ID.
6. Add diagnostics for request received, duplicate/coalesced request, shader
   chain apply, restart queued, restart complete, and final effective rule.

## Verification

1. Start the madVR renderer at 23.976 and 59.94 Hz with the configured queue
   size of 32.
2. Trigger Shift+N with a normal key press and then deliberately hold it long
   enough to create keyboard repeat.
3. Confirm one request ID, at most one shader-chain clear/install, and at most
   one renderer restart.
4. Confirm delivery resumes after normal 5-frame buffering and that converted
   queue/backpressure statistics remain healthy.
5. Confirm plain N still selects the explicit `nls_off` rule.

## Acceptance criteria

- Holding Shift+N produces one effective NLS selection and at most one
  renderer restart.
- Only one external shader-chain clear/install occurs for a single NLS change.
- VP returns to its normal converted-queue target after the controlled restart.
- No renderer restart occurs for an already active/pending identical rule.
- Logs and OSD identify requested rule, effective rule, source, and request ID.
