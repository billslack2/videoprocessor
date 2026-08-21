# VP-0111: Scope shortcuts to VP focus and restore presentation focus

## Status

Backlog (policy decided 2026-08-20). No implementation has started.

The user selected a focus-scoped model: VP keyboard commands must be active
only while a VP presentation window owns the foreground. VP must actively
request foreground focus at startup, after a renderer handoff, and when the
associated Config window closes or minimizes. Config remains an explicit
suppression boundary while it is active.

Windows can reject `SetForegroundWindow` when an unrelated application owns
the foreground. The implementation must use supported activation APIs, record
the result, and must not claim an unconditional focus guarantee that Windows
does not provide.

Next action: complete the implementation-base gate, then implement and test the
focus qualification, presentation-target activation, and Config handback as
one keyboard/focus contract.

## User story

As a VideoProcessor operator, I want VP shortcuts to work only when VP visibly
has focus, so ordinary typing in another application can never reset, rebuild,
or reconfigure the running video pipeline.

As an operator starting VP, changing renderers, or leaving Config, I want VP to
restore focus to the active video presentation automatically, so its shortcuts
work immediately without an extra mouse click or Alt+Tab.

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

## Decided focus and shortcut contract

### Shortcut eligibility

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
4. Remove or disable the normal background-command path. Do not retain a
   hidden exception for printable keys, function keys, keyboard remotes, or
   modified accelerators. Config reveal is also a VP-local command under this
   policy.
5. Preserve the existing DirectShow child-window message drain and fullscreen
   host routing so a focused renderer surface still reaches VP's authoritative
   accelerator table exactly once.

### Focus acquisition and return

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
7. Log the activation reason, target, generation, foreground before/after, API
   result, and Windows denial/fallback without logging user-entered keys.

## Windows activation boundary

Use documented foreground APIs and normal user-initiated foreground transfer.
Do not use simulated input, repeated focus loops, indefinite polling,
`AttachThreadInput` tricks, or other mechanisms that fight the user's current
foreground choice.

Startup following an interactive launch and Config-to-VP handback should
normally possess foreground permission. A renderer change occurring while an
unrelated application is foreground may be denied by Windows; VP must make one
bounded activation attempt, retain correct shortcut suppression if denied, and
emit truthful diagnostics rather than re-enabling global keyboard capture.

## Acceptance criteria

- Typing representative lowercase and uppercase prose, including `R`, `V`,
  `N`, `L`, and configured dynamic-profile letters, in an unrelated foreground
  application produces zero VP commands or renderer/profile changes.
- Function keys and Ctrl/Alt/Shift chords also produce zero VP commands while
  an unrelated application owns the foreground; the policy has no background
  shortcut exceptions.
- The same suppression applies while typing or using popups in Config, even if
  Config Save/Apply rebuilds or switches the renderer behind it.
- With VP foreground, all configured accelerators continue to dispatch exactly
  once from the main window, windowed video, fullscreen host, and supported
  DirectShow/madVR child-window focus paths.
- A normal interactive VP start activates the ready presentation target when
  Windows grants foreground permission; the result is logged and shortcuts
  work without a click.
- Alpha/VP Renderer to madVR, madVR to Alpha/VP Renderer, and same-backend
  reconstruction activate only the current generation's target after its first
  live frame. No retiring/stale HWND receives focus and no duplicate command is
  produced across the handoff.
- Closing Config to the tray and minimizing Config both return focus to the
  latest VP presentation target in windowed and fullscreen modes. Shortcuts
  resume immediately after the handback.
- A renderer change while Config remains active does not raise VP above Config
  or disrupt an active Config popup. Closing/minimizing Config afterward
  returns to the replacement renderer target.
- If Windows denies an activation because another application owns the
  foreground, VP remains background-safe, performs no retry loop, and logs a
  bounded, truthful failure result.
- No focus operation changes queue policy, capture timing, renderer lifetime,
  display topology, configuration values, or first-live-frame shielding.
- A clean x64 Release build, native focus-policy tests, Config UI tests, and
  the live windowed/fullscreen renderer-switch matrix pass.

## Required tests

1. Policy unit tests covering every foreground class: VP main window,
   windowed host, fullscreen host, routed renderer child, Config, unrelated
   process, desktop/null, destroyed HWND, and stale renderer generation.
2. Accelerator tests covering bare letters, Shift-only letters, Ctrl/Alt
   chords, function keys, built-ins, and dynamic configured commands with VP
   foreground and non-VP foreground.
3. Startup tests proving activation is requested only after the selected
   presentation target exists and that an OS denial leaves shortcuts disabled
   in the background.
4. Renderer-transition tests proving focus is assigned at the matching
   first-live-frame reveal, never during retirement or to an older generation.
5. Config tests for close-to-tray, explicit minimize, renderer target change
   while visible, popup activity during Apply, stale target rejection, and
   return to windowed/fullscreen targets.
6. End-to-end testing that types `Rendering video` in another process and in
   Config with zero VP `WM_COMMAND` dispatches, then restores VP focus and
   proves the same configured shortcuts work once each.
7. Live Alpha/madVR round trips in windowed, windowed-fullscreen, and supported
   exclusive presentation modes, including Config open during the handoff.

## Non-goals

- Preserving unrestricted background keyboard or keyboard-remote control.
- Circumventing Windows foreground-lock policy or guaranteeing focus theft
  from an unrelated application.
- Changing renderer queues, timing, display-state restoration, shader/profile
  semantics, fullscreen monitor selection, or configuration persistence.
- Repairing subtitle detection, placement, crop authority, or NLS behavior.

## Related work

- VP-0073: authoritative keyboard routing through renderer handoffs.
- VP-0097: standalone Config ownership, activation, and tray integration.
- VP-0102: Modern UI and the current background observer.
- VP-0103: live Config apply and renderer lifecycle publication.
- VP-0105: configurable runtime UI shortcut.
- VP-0118: fullscreen shortcut state and Right Alt normalization.
- VP-0134: generation-safe symmetric renderer handoff and first-frame reveal.
