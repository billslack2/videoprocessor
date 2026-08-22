# VP-0111: Configurable foreground shortcuts and focus restoration

## Status

Backlog (requirement revised 2026-08-22). No implementation has started.

The user chose an opt-in compatibility model. Keep one `Shortcuts` item in the
left side menu. Inside that page, add `Setup` and `Shortcuts` tabs in the same
style already used inside the Shaders and VP Renderer pages. The Setup tab's
checkbox enables the focus-scoped behavior defined below. It defaults to
unchecked, which must retain today's global shortcut and focus behavior. The
existing shortcut editor moves intact to the internal Shortcuts tab.

GitHub currently reports `v1.2.001-beta` as the VideoProcessor default
integration branch. Implementation is waiting for the developer to confirm
that branch as the base, as required by the tracker implementation gate.

Windows can reject `SetForegroundWindow` when an unrelated application owns
the foreground. Focus-only mode must use supported activation APIs, record the
result, and must not claim an unconditional focus guarantee that Windows does
not provide.

## User story

As a VideoProcessor operator, I want a setting that limits VP shortcuts to
times when VP visibly has focus, so ordinary typing in another application
cannot reset, rebuild, or reconfigure the running video pipeline.

As an operator using that focus-only mode, I want VP to restore focus to its
active presentation when VP starts, a renderer changes, or Config closes or
minimizes, so shortcuts work immediately without an extra click or Alt+Tab.

As an existing operator who relies on global shortcuts or a keyboard remote, I
want the new setting to default off and preserve current behavior until I
explicitly opt in.

## Observed behavior

On 2026-08-10, VP was running in Modern UI mode behind another application.
The low-level keyboard observer accepted unmodified printable keys whenever
the foreground process was not the VP host process and left the same keys
available to the foreground application.

The live log recorded background dispatches while an external PID owned the
foreground:

- `V` repeatedly selected video conversion off and reconstructed the renderer.
- `R` reset the renderer.
- `N` selected a configured shader rule.
- Dynamic unified-profile keys also dispatched from ordinary printable input.

The result was visible black/reveal flashing in VP while the user typed into
another application. Shift-only letters are not safe because normal uppercase
typing produces the same modifier state.

Current source inspection also shows that:

1. Modern UI starts a process-wide `WH_KEYBOARD_LL` observer.
2. The observer deliberately accepts every configured accelerator, including
   bare keys, whenever a non-VP process owns the foreground.
3. Config is detected as a special external foreground process and suppresses
   dispatch only while it owns focus.
4. The delayed fullscreen pass deliberately preserves another process's focus
   and activates only when VP already owns the foreground or no foreground
   window exists.
5. Config close-to-tray hides and clears its native owner but does not hand
   focus back to VP. Config minimize has no equivalent handback handler.
6. VP already publishes a validated current presentation target to Config and
   renderer transitions already identify the first live frame of a replacement
   renderer.

## Configuration and UI contract

1. Add a Boolean `[shortcuts]` setting named `foreground_only`.
2. If the setting or the entire section is absent, its effective value is
   `false`. Existing configurations therefore retain current behavior.
3. Accept only the configuration parser's normal Boolean spellings and report
   an invalid value through the existing validation surface.
4. Apply a saved change live to a running VP. Changing the checkbox must start
   or stop the background observer as required without a VP restart or renderer
   rebuild.
5. Keep exactly one `Shortcuts` entry in the left side menu. Do not add Setup
   or a second Shortcuts entry to the side menu.
6. Inside the page selected by that side-menu entry, follow the established
   Shaders and VP Renderer internal-tab pattern:
   - `Setup` is the first internal tab.
   - `Shortcuts` is the second internal tab and contains the complete existing
     shortcut editor unchanged.
7. For now, Setup contains one keyboard-handling card with one checkbox:
   **Only process shortcuts while VideoProcessor is in the foreground**.
8. Setup help text must explain both modes plainly:
   - Checked: background keystrokes do not control VP; VP attempts to restore
     focus at the defined lifecycle boundaries.
   - Unchecked: configured shortcuts retain today's global behavior whenever
     Config is not active.
9. Update `VideoProcessor.cfg` comments and `CONFIGURATION.html` with the
   default, live-apply behavior, Config exception, and Windows focus limitation.

## Mode behavior

### Unchecked: compatibility/global mode

1. Preserve the current low-level shortcut observer and its command coverage,
   including bare keys, modified accelerators, dynamic bindings, and the
   existing Config suppression rule.
2. Preserve current startup, renderer-transition, fullscreen, and Config
   close/minimize focus behavior. Do not introduce the new focus-reclamation
   actions in this mode.
3. Do not silently rewrite existing shortcut bindings or require migration.

### Checked: foreground-only mode

#### Shortcut eligibility

1. Dispatch a VP shortcut only when the foreground belongs to the current VP
   presentation hierarchy. Qualifying targets include the VP main/operator
   window, windowed video host, fullscreen host, and renderer child windows
   whose input is explicitly drained or routed to that VP instance.
2. Do not dispatch VP commands when an unrelated application, the desktop,
   an unknown HWND/PID, or the associated standalone Config process owns the
   foreground.
3. Apply this rule uniformly to bare keys, modified chords, built-in commands,
   configured shortcuts, dynamic profile/shader/display rules, renderer
   selection, reset/restart, capture, fullscreen, and UI commands.
4. Disable the background-command path completely in this mode. Config reveal
   is also a VP-local command.
5. Preserve the DirectShow child-window message drain and fullscreen host
   routing so a focused renderer surface still reaches VP's authoritative
   accelerator table exactly once.

#### Focus acquisition and return

1. At normal interactive startup, request foreground activation only after the
   intended VP presentation target exists and is ready to receive input.
2. After a renderer selection, reconstruction, or fullscreen retarget that
   replaces the active presentation surface, activate the new target at the
   generation-matched first-live-frame reveal boundary. Do not focus a retiring
   HWND or steal focus at the beginning of a black transition.
3. Never reclaim focus from Config while Config is visible and active,
   including while Save/Apply triggers a renderer rebuild or while a Config
   popup, combo, or menu is open.
4. When Config closes to the tray, hides, or becomes minimized by an explicit
   user action, it must hand focus to VP's latest validated presentation
   target: active fullscreen host first, otherwise the stable main/windowed
   presentation root.
5. Config-to-VP handback is cross-process. Validate the target PID and HWND,
   use the foreground privilege held by the active Config process where
   appropriate, and reject stale, destroyed, foreign, or superseded targets.
6. A renderer-generation change during Config use must update the published
   target without raising VP. The eventual close/minimize handback must use the
   newest valid target.
7. Log the mode, activation reason, target, generation, foreground before and
   after, API result, and Windows denial/fallback without logging user text.

## Windows activation boundary

Use documented foreground APIs and normal user-initiated foreground transfer.
Do not use simulated input, repeated focus loops, indefinite polling,
`AttachThreadInput` tricks, or other mechanisms that fight the user's current
foreground choice.

Startup following an interactive launch and Config-to-VP handback should
normally possess foreground permission. A renderer change occurring while an
unrelated application is foreground may be denied by Windows; focus-only mode
must make one bounded activation attempt, retain correct shortcut suppression
if denied, and emit truthful diagnostics rather than falling back to global
keyboard capture.

## Acceptance criteria

- The left side menu still contains exactly one `Shortcuts` item. Selecting it
  shows internal `Setup` and `Shortcuts` tabs using the same visual and
  interaction pattern as the Shaders and VP Renderer tabs.
- Setup contains the single foreground-only checkbox, initially unchecked for
  an absent setting. The existing shortcut fields, defaults, clear controls,
  validation, scrolling, and save behavior remain unchanged on the internal
  Shortcuts tab.
- Saving the checkbox writes only `[shortcuts] foreground_only`; loading,
  saving, and live apply preserve unrelated settings, comments, and bindings.
- Toggling the setting live changes keyboard policy without rebuilding the
  renderer or restarting VP.
- With the checkbox unchecked, the current global shortcut behavior and Config
  exception remain unchanged, including representative bare, modified, and
  dynamic bindings while another application owns the foreground.
- With the checkbox checked, typing representative lowercase and uppercase
  prose, including `R`, `V`, `N`, `L`, and configured dynamic-profile letters,
  in another foreground application produces zero VP commands or renderer or
  profile changes.
- In checked mode, function keys and Ctrl/Alt/Shift chords also produce zero VP
  commands while an unrelated application owns the foreground; there are no
  background shortcut exceptions.
- In checked mode, the same suppression applies while typing or using popups
  in Config, even if Save/Apply rebuilds or switches the renderer behind it.
- With VP foreground in either mode, every configured accelerator continues to
  dispatch exactly once from the main window, windowed video, fullscreen host,
  and supported DirectShow/madVR child-window focus paths.
- In checked mode, a normal interactive VP start activates the ready
  presentation target when Windows grants foreground permission; the result is
  logged and shortcuts work without a click.
- In checked mode, Alpha/VP Renderer to madVR, madVR to Alpha/VP Renderer, and
  same-backend reconstruction activate only the current generation's target
  after its first live frame. No retiring/stale HWND receives focus.
- In checked mode, closing Config to the tray and minimizing Config return
  focus to the latest VP presentation target in windowed and fullscreen modes.
- A renderer change while Config remains active does not raise VP above Config
  or disrupt an active Config popup. In checked mode, closing or minimizing
  Config afterward returns to the replacement renderer target.
- If Windows denies an activation, VP performs no retry loop, retains the
  selected shortcut policy, and logs a bounded, truthful failure result.
- No mode or focus operation changes queue policy, capture timing, renderer
  lifetime, display topology, configuration values beyond the new setting, or
  first-live-frame shielding.
- A clean x64 Release build, native focus-policy tests, Config UI tests, and
  the live windowed/fullscreen renderer-switch matrix pass in both modes.

## Required tests

1. Configuration tests for absent/default false, explicit false, explicit
   true, invalid Boolean text, round-trip preservation, and live apply.
2. Config UI tests for the unchanged single Shortcuts side-menu item, internal
   Setup/Shortcuts tabs, initial checkbox state, save/reload, dirty state,
   validation, and preservation of every existing shortcut field.
3. Policy unit tests in both modes covering VP main window, windowed host,
   fullscreen host, routed renderer child, Config, unrelated process,
   desktop/null, destroyed HWND, and stale renderer generation.
4. Accelerator tests in both modes covering bare letters, Shift-only letters,
   Ctrl/Alt chords, function keys, built-ins, and dynamic configured commands.
5. Checked-mode startup tests proving activation is requested only after the
   selected presentation target exists and that an OS denial leaves shortcuts
   suppressed in the background.
6. Checked-mode renderer-transition tests proving focus is assigned at the
   matching first-live-frame reveal, never during retirement or to an older
   generation; unchecked-mode tests prove the new activation is absent.
7. Config tests for close-to-tray, explicit minimize, renderer target change
   while visible, popup activity during Apply, stale target rejection, and
   conditional return to windowed/fullscreen targets by mode.
8. End-to-end testing that types `Rendering video` in another process and in
   Config, switches modes live, and proves the expected zero/global dispatch
   behavior without duplicate commands.
9. Live Alpha/madVR round trips in windowed, windowed-fullscreen, and supported
   exclusive presentation modes, including Config open during the handoff.

## Non-goals

- Changing the current global behavior when the checkbox is unchecked.
- Circumventing Windows foreground-lock policy or guaranteeing focus theft
  from an unrelated application.
- Changing renderer queues, timing, display-state restoration, shader/profile
  semantics, fullscreen monitor selection, or unrelated configuration.
- Repairing subtitle detection, placement, crop authority, or NLS behavior.

## Related work

- VP-0073: authoritative keyboard routing through renderer handoffs.
- VP-0097: standalone Config ownership, activation, and tray integration.
- VP-0102: Modern UI and the current background observer.
- VP-0103: live Config apply and renderer lifecycle publication.
- VP-0105: configurable runtime UI shortcut.
- VP-0118: fullscreen shortcut state and Right Alt normalization.
- VP-0134: generation-safe symmetric renderer handoff and first-frame reveal.
