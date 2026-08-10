# VP-0111: Prevent background shortcuts from hijacking normal typing

## Status

Backlog (2026-08-10). Deferred during VP-0110 live validation after the user
observed the Windows desktop and VP preview flashing while VP was running in
the background. Current logs and source inspection identify background
keyboard-command collisions as the direct cause. No implementation has
started.

Next action: choose and document a background-shortcut activation model that
keeps deliberate theater/remote-control operation available without treating
ordinary text entry as VP control input. Then complete the implementation-base
gate before starting source work.

## User story

As a VideoProcessor user who may temporarily use another Windows application
while VP remains running, I want ordinary typing to remain private to the
foreground application, so letters entered in a browser, Codex, an editor, or
VP's configuration editor cannot reset, rebuild, or reconfigure the running
video pipeline.

As a theater operator, I still want explicitly configured background controls
to work while a renderer or other approved application owns the foreground,
including controls sent by a keyboard-style remote.

## Observed failure

On 2026-08-10, VP was running in Modern UI mode behind another application.
The low-level keyboard observer accepted unmodified printable keys whenever
the foreground process was not the VP host process. The original key was also
left available to the foreground application.

The live log recorded the resulting command dispatches while the foreground
PID was external:

- `command=32810` repeatedly selected video conversion off whenever `V` was
  typed. That path reconstructed the renderer and produced visible black/reveal
  flashes in the VP preview.
- `command=32780` reset the renderer whenever `R` was typed.
- `command=32900` selected the configured `N` shader rule while `N` was typed.
- Dynamic unified-profile commands were also dispatched from printable profile
  shortcuts.

The active `[shortcuts]` section did not override these commands; unspecified
built-in defaults remained active. `Shift+R` is not a safe distinction because
typing an uppercase `R` can invoke it. The observer suppresses key-repeat only
until key-up, so every separate press can dispatch another command.

This is separate from VP-0110 subtitle-placement timing. The subtitle engage
drift remained configured at 250 ms during that validation; changing it does
not address background keyboard collisions.

## Current behavior and risks

1. The Modern UI starts a system-wide `WH_KEYBOARD_LL` observer while its
   normal UI is enabled.
2. `MayDispatchGlobalShortcut` treats any nonzero foreground PID other than
   the VP host PID as eligible. An associated out-of-process configuration
   editor and unrelated desktop applications therefore both look external.
3. All configured accelerators are offered to the observer, including bare
   letters and Shift-only letters used in normal prose.
4. Most matched events use `consume=0`, so one key can act in both VP and the
   foreground application. This makes the collision easy to miss until a VP
   side effect is visible.
5. Commands have unequal consequences: a shader selection may be a no-op, but
   renderer reset, restart, video-conversion changes, renderer selection, and
   some profile changes can cause a black transition, rebuild, or capture
   disruption.
6. Simply disabling all background shortcuts would regress valid fullscreen,
   madVR, automation, and keyboard-remote workflows.

## Scope

1. Define a single, explicit policy for whether each configured shortcut is
   local to VP, safe for background dispatch, or deliberately opted into
   unrestricted global dispatch.
2. Make ordinary printable text safe by default. Bare alphanumeric keys and
   Shift-only alphanumeric keys must not control VP while an unrelated desktop
   application or VP's associated configuration editor owns the foreground.
3. Preserve foreground VP accelerators and provide a clear route for global
   theater controls. Suitable defaults may include non-text function/media
   keys or combinations containing a non-Shift modifier; users who require a
   bare-key remote workflow need an explicit, visible opt-in rather than an
   implicit default.
4. Treat all windows belonging to the same VP installation coherently. Text
   entry in the standalone configuration editor must not dispatch commands to
   the host behind it, while the intended global editor reveal/hide command
   remains functional.
5. Classify renderer reset/restart, conversion, renderer selection, shader,
   display-rule, unified-profile, capture, UI-toggle, and dynamic action
   shortcuts under the same policy. Do not repair only the observed `V`
   binding.
6. Decide and document foreground-event consumption. Avoid an accidental
   double action in VP and a foreground renderer/application, while preserving
   shortcuts that intentionally must pass through.
7. Update the configuration UI and reference documentation so users can tell
   whether a binding is VP-local or background/global, and warn about unsafe
   printable global bindings before saving them.
8. Add command-level diagnostics that identify the binding and policy reason
   for a background dispatch or rejection without logging user-entered text.

## Acceptance criteria

- Typing representative prose containing lowercase and uppercase `R`, `V`,
  `N`, `L`, and configured dynamic-profile letters in an unrelated foreground
  application causes zero VP commands, renderer resets, renderer rebuilds,
  conversion changes, shader changes, or profile changes under safe defaults.
- The same safety applies while entering text or numeric configuration values
  in VP's associated standalone configuration editor.
- VP-owned foreground accelerators continue to work as configured.
- At least one documented safe background-control scheme works while madVR or
  another approved presentation window is foreground, and an explicit opt-in
  can preserve users' intentional bare-key remote behavior.
- Renderer reset/restart and conversion commands cannot be triggered by normal
  background typing under a default or migrated configuration.
- Configuration migration does not silently convert an existing intentional
  global binding into a different command, and users are told when a legacy
  printable binding is restricted or requires opt-in.
- The original key's consumption/pass-through behavior is deterministic and
  covered for both accepted and rejected background shortcuts.
- A clean x64 Release build and the relevant configuration/live-apply tests
  pass.

## Required tests

1. Policy unit tests for VP foreground, associated configuration-editor
   foreground, approved renderer foreground, unrelated application foreground,
   zero/unknown foreground PID, and configuration-modal state.
2. A shortcut matrix covering unmodified letters, Shift-only letters,
   Ctrl/Alt combinations, function keys, and explicitly opted-in printable
   global keys.
3. Command-family tests covering renderer reset/restart, conversion, shader,
   display rule, renderer selection, unified profile, capture, configuration
   editor, and no-UI toggle bindings.
4. An end-to-end observer test proving a sequence such as `Rendering video`
   typed into another process produces no VP `WM_COMMAND` dispatches under
   safe defaults.
5. Regression tests proving an approved background theater shortcut dispatches
   exactly once, key auto-repeat is bounded as intended, and the configured
   consume/pass-through policy is honored.
6. Migration tests for configurations that omit the built-in shortcuts and
   for configurations that intentionally define legacy bare-letter bindings.

## Non-goals

- Changing subtitle detection, subtitle placement, crop authority, NLS
  geometry, or VP-0110 engage/release interpolation.
- Eliminating keyboard-remote or fullscreen background control.
- Hiding genuine renderer transitions that were deliberately requested.

## Related work

- VP-0097: standalone configuration editor and VP integration.
- VP-0102: Modern UI mode, where the background observer is active.
- VP-0103: safe live configuration publication and renderer lifecycle.
- VP-0105: configurable runtime no-UI shortcut.
- VP-0110: subtitle-placement timing; its live validation exposed this
  independent keyboard issue.
