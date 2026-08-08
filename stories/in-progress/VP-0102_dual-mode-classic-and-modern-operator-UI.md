# VP-0102: Dual-mode classic and modern operator UI

## Status

In Progress. Source work is authorized on `codex/vp-0102-modern-ui`, based on
the latest approved GitHub default branch `v1.1.017-beta` at
`3c7ebd5315cd7396669d1762822a94cfa358d490` when the worktree was created.
The current classic dialog remains the effective default and must remain fully
functional throughout implementation.

VP-0097 is the dependency for exposing the persisted `interface` preference in
the configuration editor. Design, read-only telemetry extraction, and toolkit
feasibility work may proceed independently, but this story cannot be accepted
until the setting is safely editable through that editor.

## User story

As a VP operator, I want a compact, attractive modern control surface that
shows the live capture and renderer health I need beside a 16:9 video preview,
without exposing runtime configuration controls, while retaining the current
classic dialog exactly as a safe familiar interface.

## Product decision

VP has two selectable application interfaces:

- **Classic**: the existing dialog, unchanged in capability, behavior,
  shortcuts, sizing behavior, and default startup selection.
- **Modern**: a separate, information-first operator UI with a left status and
  operational-actions column plus a right 16:9 video-preview area.

Classic remains the effective default until the developer deliberately changes
that policy in a future story. This story does not remove, hide, deprecate, or
reimplement the classic UI.

## Interface selection contract

1. Add the command-line switch `/interface [classic|modern]`. Matching is
   case-insensitive; invalid or incomplete values log one clear error and fall
   back to the normal resolved selection rather than failing capture startup.
2. Add a persisted shared application setting named `interface` with allowed
   values `classic` and `modern`. Its configuration location, schema/default,
   sample entry, and documentation must follow the current canonical shared
   configuration model and be exposed by VP-0097's configuration editor as a
   user-facing Startup choice.
3. Selection precedence is fixed and documented:

   1. `/noui` wins and shows no control interface.
   2. A valid `/interface` value overrides persisted configuration for that
      process only.
   3. The persisted `interface` value applies when valid.
   4. Missing or invalid configuration resolves to `classic`.
4. `noui` remains supported. Its video window defaults to a 16:9 client area
   and can be resized down to a minimum video area of 320x180. It must not
   instantiate Classic or Modern controls, create editable controls, or change
   capture/renderer behavior merely to satisfy layout.
5. Classic and Modern persist/restore window placement safely and independently
   if VP currently persists placement. A stored Classic size or position must
   never create an undersized, off-screen, or malformed Modern window, and the
   reverse must also hold.

## Modern UI behavior and content

The Modern UI is an operator dashboard, not a second configuration editor.
Except for the three existing operational actions and the configuration-editor
entry action below, every item is read-only. Changing capture, color, renderer,
queue, timing, conversion, fullscreen, or profile settings remains the
responsibility of the configuration file and VP-0097 editor.

### Left-side operator information

Use clear cards, compact metric rows, or another designer-approved grouping to
show these existing live data items at the same effective update cadence as the
current dialog:

1. **Capture device**: selected capture-device name, capture state, and the
   existing capture restart action. Do not show the HDMI/connection selector.
2. **Input and captured video**: lock/valid state, resolution, frame rate,
   pixel format or chroma/bit-depth summary, frame/miss counters, capture
   hardware/latency values where available, and other currently meaningful
   read-only input/captured-video facts.
3. **Hardware link**: PCIe link speed and width, plus any existing device
   properties that are useful for diagnosing capture health.
4. **HDR luminance**: current MaxCLL, MaxFALL, display/mastering luminance,
   and effective HDR/LLDV-derived values already shown by the classic UI. They
   are live information, not editable fields.
5. **Renderer**: active renderer name, renderer state, and the existing
   renderer restart action. Do not expose a renderer-selection dropdown or any
   renderer configuration controls.
6. **Queue**: current VP/raw/transformed/target queue health, configured target
   or capacity where useful, drop/repeat counts, and the existing manual Reset
   action. Queue configuration remains outside this UI.
7. **Latency / timing**: the current timing/latency values and labels, updated
   live at the same cadence as the classic Timing group. Use human-readable
   labels; preserve the actual data semantics rather than relabeling a value
   incorrectly.
8. **Configuration**: include a well-laid-out, always discoverable **Open
   configuration** action. It launches or activates the existing standalone
   configuration editor using VP's resolved primary configuration path and the
   Modern window as owner, following the same single-instance and safe-launch
   behavior as the existing configuration-editor command/shortcut. It changes
   no configuration itself. If the editor is unavailable or fails to start,
   show a concise actionable status without affecting capture or rendering.

The modern layout must make state, failures, disabled/unavailable values, and
the restart/reset/configuration actions immediately distinguishable from
ordinary text. An action must invoke the same existing controller command and
safety behavior as its Classic counterpart, or the existing configuration-editor
launch path; the Modern UI must not duplicate or reinterpret lifecycle or
configuration-save logic.

### Right-side video area

1. Keep the existing video/renderer output host and presentation pipeline. The
   new UI must not introduce a separate capture path, renderer, conversion,
   frame queue, timing policy, or compositor merely to show the preview.
2. Place the video area on the right. Its default visible host rectangle is
   16:9; video with a different content aspect must retain its normal renderer
   aspect behavior inside that host rather than being stretched to 16:9.
3. Remove the chromaticity/color-gamut diagram from Modern UI. Do not remove it
   from Classic UI.
4. The Modern UI default/minimum dimensions must be selected by the approved
   visual design, be noticeably wider than tall, and guarantee a usable 16:9
   preview plus all mandatory left-side information without clipping. The user
   must not resize below that approved default/minimum. Record the final client
   and outer-window metrics in the implementation evidence.
5. Resize behavior must preserve a usable left information column and maintain
   the right preview's 16:9 host geometry. At larger sizes, extra space should
   benefit the preview first without producing awkward empty cards or clipped
   telemetry.

## Architecture and safety boundary

1. Separate UI-neutral runtime observation and command invocation from concrete
   presentation. The Classic dialog must keep using its existing behavior; the
   Modern UI consumes the same read-only snapshots and dispatches the same
   capture-restart, renderer-restart, queue-reset, and configuration-editor
   launch commands.
2. Do not move capture, color/HDR, renderer, queue, timing, fullscreen, or
   device-selection business rules into a new UI toolkit. Extract only narrow,
   testable presentation adapters if the existing dialog currently owns access
   to live state.
3. Evaluate Qt 6 Widgets as a candidate because VP-0097 already establishes a
   Qt-based editor and its deployment approach. Accept Qt only if it can host
   the existing video child/output window, participate safely in VP's Win32
   message, accelerator, tray, fullscreen, DPI, and shutdown lifecycles, and
   ship without requiring a second independently managed UI loop. A native
   MFC/Win32 Modern view is an acceptable outcome if it better preserves those
   boundaries.
4. Record a brief toolkit decision before implementation: chosen approach,
   video-host integration, ownership/threading, message/keyboard handling,
   DPI policy, deployment dependencies, Classic isolation, and rollback path.
5. The renderer and capture pipeline must start and recover identically for
   Classic, Modern, and `noui`; an interface switch may change window/view
   construction, never frame delivery or color/timing behavior.

## Required visual design and reviews

Before production source changes, use one UI designer to create the Modern UI
design package based on the supplied Classic screenshot and the mandatory
information/action inventory. It must include annotated normal, compact/minimum,
and enlarged/DPI-aware wireframes; typography, spacing, color/status semantics;
keyboard focus; High Contrast behavior; and explicit 16:9 preview geometry.

Two independent UI/design reviewers must then review the package and its first
working implementation:

1. **Visual hierarchy review**: readability at normal viewing distance,
   information grouping, action prominence, 16:9 preview balance, resizing,
   and avoiding empty or dense regions.
2. **Operator/accessibility review**: read-only versus actionable clarity,
   keyboard navigation, focus order, screen-reader names, DPI 100/150/200%,
   High Contrast, failure states, and prevention of accidental configuration
   editing.

Record each reviewer's findings, decisions, and screenshots in the story when
work moves to Review. Neither review substitutes for Classic regression or live
capture/renderer validation.

## Implementation increments

1. Document the interface-selection/configuration contract and add parser/schema
   tests. Keep the effective default Classic.
2. Build/test a UI-neutral live-status snapshot plus shared operational-command
   adapter. Prove Classic behavior and update cadence are unchanged.
3. Complete toolkit feasibility and the designer-approved modern layout before
   wiring live VP state.
4. Implement Modern read-only cards, shared restart/reset actions, the shared
   configuration-editor entry action, 16:9 video host sizing, and independent
   window placement.
5. Implement `noui` 16:9 default/minimum sizing without creating dashboard
   controls.
6. Add the persisted setting to VP-0097, sample configuration, and complete
   public help. Conduct the two design reviews and full regression validation.

## Non-goals

- Do not remove, simplify, or regress the Classic UI.
- Do not make configuration values editable from Modern UI.
- Do not replace the standalone configuration editor or duplicate its profile,
  shader, renderer, queue, color, or shortcut editing surfaces.
- Do not replace the existing video renderer/output host or alter capture,
  conversion, queue, latency, HDR, fullscreen, or renderer lifecycle policy.
- Do not add a web UI, remote control, or new renderer as part of this story.
- Do not change the existing `noui` behavior beyond the defined 16:9 default
  and 320x180 minimum video-area sizing.

## Validation and acceptance criteria

1. `classic`, `modern`, invalid `/interface`, persisted selection, command-line
   override, and `/noui` precedence have automated parser/configuration tests
   and concise startup logs showing the resolved mode and reason.
2. Classic startup is the default with no new argument/configuration. A focused
   Classic regression pass verifies capture restart, renderer restart, queue
   reset, all existing editable controls, keyboard shortcuts, fullscreen,
   renderer switching, timer cadence, and video output behave as before.
3. Modern UI has no editable configuration fields, renderer/capture dropdowns,
   HDMI selector, gamut diagram, or hidden configuration mutation path. Its
   only interactive controls are capture restart, renderer restart, queue
   reset, and Open configuration. The first three are proven to invoke the same
   command path as Classic; Open configuration is proven to invoke the existing
   resolved-path, owner-aware, single-instance editor-launch path without
   mutating configuration in the Modern UI process.
4. Modern live data matches Classic for identical capture/renderer conditions,
   including unavailable/error states and update cadence. Tests must cover no
   capture device, unlocked input, no active renderer, queue recovery, HDR/SDR,
   and renderer restart.
5. The right preview host is 16:9 by default and through resizing; content is
   not geometrically stretched. The final Modern default/minimum supports all
   required left-side items and a usable 16:9 preview with no clipping at
   100%, 150%, and 200% DPI.
6. `noui` defaults to 16:9 and can resize to a 320x180 minimum video area. It
   does not create dashboard/classic controls or change capture/renderer
   behavior.
7. Qt, if selected, passes a real executable deployment/startup test with its
   required runtime dependencies. Any native-toolkit choice receives equivalent
   startup, DPI, accelerator, child-window, and shutdown validation.
8. Both design reviews are recorded and their accepted fixes are included.
9. x64 Release builds and relevant automated/integration tests pass. Live
   validation covers both renderers, capture restart, renderer restart, queue
   reset, fullscreen/windowed transitions, display/refresh changes, and clean
   shutdown without stale frames, dropped input commands, UI freeze, or queue
   regression.
10. The Modern Open configuration action reliably launches or activates the
    editor against the resolved configuration file, including when an editor is
    already in the notification area. Its unavailable/failure state is clear
    and non-disruptive.
11. VP-0097 exposes the persisted interface choice, and the shipped sample plus
    `CONFIGURATION.html` clearly describe Classic default, Modern selection,
    `/interface`, `/noui`, precedence, sizing behavior, and how to open the
    configuration editor.

## Dependencies and references

- VP-0097: persisted configuration-editor surface for `interface`.
- Existing main dialog and live-status/controller code, to be identified during
  the toolkit decision without broad business-logic rewrite.
- Existing output/fullscreen behavior, including VP-0094 and VP-0095 where
  their final monitor/session behavior overlaps the shared video host.
- User-provided Classic UI screenshot:
  `C:\Users\bslac\AppData\Local\Temp\codex-clipboard-f4f3e0d1-838a-4de4-90b7-a354843cc516.png`.
