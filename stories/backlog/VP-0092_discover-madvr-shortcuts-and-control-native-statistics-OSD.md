# VP-0092: Discover madVR shortcuts and control its native statistics OSD

## Status

Backlog. No implementation branch has been selected.

## User story

As a VideoProcessor user who customizes madVR keyboard shortcuts, I want VP to
discover the active madVR bindings and invoke the matching actions directly,
so VP can toggle madVR's native statistics OSD and reset its dropped/repeated
frame counters without duplicate VP configuration, window focus changes, or
global keyboard injection.

## Context

VP already uses `IMadVROsdServices` to compose and remove VP's own named
diagnostic bitmap overlay. That is distinct from madVR's built-in Debug OSD and
its internal statistics counters.

### Installed-settings integration evidence (2026-08-09)

The operator verified that the installed madVR settings application at
`C:\madvr210\madHcCtrl.exe` works normally with VP. Its settings window is
intentionally modal over the entire desktop, not just the VP window. This is
accepted existing madVR behavior and must not be treated as a VP compatibility
failure or an accessibility/focus regression. This story must not attempt to
automate, re-parent, suppress, or change that native settings window; its
scope remains in-process discovery and invocation of the relevant playback
commands only.

madVR exposes two relevant public interfaces to an in-process DirectShow host:

- `IMadVRSettings2`, which can enumerate persisted madVR settings and profiles;
- `IMadVRCommand::SendCommandInt("keyPress", ...)`, which madVR added for
  media-player hosts to invoke its configured shortcut actions.

The user has mapped `F12` to madVR's clear-statistics action. The implementation
must not require VP to repeat that mapping in `VideoProcessor.cfg`. Instead it
must discover the current binding when madVR is constructed. A setting edited in
madVR must be picked up by the next madVR renderer build/restart.

## Scope

1. Obtain the appropriate madVR settings and command interfaces from the active
   in-process madVR filter only.
2. Read and identify the effective keyboard bindings for:
   - toggling madVR's native Debug/statistics OSD; and
   - clearing/resetting madVR's native dropped/repeated-frame statistics.
3. Add VP actions that invoke those discovered bindings through madVR's
   in-process command interface. Do not use `SendInput`, foreground-window
   changes, cross-process automation, or `PostMessage` to an arbitrary window.
4. Determine whether madVR's documented live Debug-OSD setting command provides
   a reliable direct on/off operation. Prefer that operation for explicit
   on/off behavior only if it is verified against the installed madVR build;
   retain the discovered toggle shortcut when only toggle semantics are
   available.
5. Keep VP's own diagnostics OSD independent: showing, hiding, or clearing it
   must not unintentionally change madVR's native OSD, and vice versa.
6. Add bounded diagnostics, for example:
   - discovery succeeded/failed and the discovered human-readable bindings;
   - selected dispatch mechanism and HRESULT/result;
   - action ignored because the active renderer is not madVR, the interface is
     unavailable, or the binding could not be resolved.

## Safety and compatibility contract

- The implementation is read-only with respect to madVR settings. It must never
  write, normalize, or repair keyboard shortcuts.
- Read settings afresh on each madVR renderer build. Do not cache a binding
  across renderer generations or persist it in VP state/configuration.
- Treat individual setting names and encodings as runtime-discovered values,
  not a compile-time assumption. If the relevant binding cannot be discovered,
  expose no misleading fallback key and log the reason once per renderer
  generation.
- A missing/older madVR interface must leave VP fully usable; only the native
  madVR-control action is disabled.
- The action must never affect an unrelated foreground application or Alpha
  renderer session.

## Investigation required before UI binding

1. Inspect the installed public `mvrInterfaces.h` and the current madVR
   settings enumeration to establish the exact setting key(s), value format,
   modifiers, and action identifiers for Debug OSD toggle and statistics reset.
2. Confirm whether the shortcut is global or profile-dependent, and read the
   same effective scope that madVR uses at playback time.
3. Validate the exact `keyPress` integer encoding for function keys and
   modifiers with a non-destructive test binding before wiring user commands.
4. Verify whether a direct `DebugOsd` command is genuinely live and has
   deterministic on/off semantics; do not infer this from a persisted setting.

## Explicit exclusions

- Do not emulate or modify physical keyboard input globally.
- Do not add a second VP configuration field for madVR's F12/Ctrl+J bindings.
- Do not change madVR queue settings, rendering policy, frame delivery, timing,
  OSD placement, or VP's Alpha renderer behavior.
- Do not claim that VP can observe or verify madVR's internal counter values;
  madVR queue/counter occupancy remains unobservable through the current
  runtime-information interface.

## Verification

1. Configure non-default madVR shortcuts (including `F12` for reset) and start
   VP with madVR selected.
2. Confirm the log reports the actual discovered bindings after each madVR
   renderer build/restart.
3. Trigger VP's native-OSD toggle and reset actions while madVR is active;
   verify the intended native madVR behavior manually.
4. Change the madVR binding, restart only the madVR renderer, and confirm VP
   discovers and uses the new value without any VP config edit.
5. Switch to Alpha or no renderer and confirm these actions are harmlessly
   ignored with one clear diagnostic.
6. Confirm VP's named diagnostics OSD can still be toggled independently in
   windowed, fullscreen-windowed, and exclusive madVR playback.

## Acceptance criteria

- VP discovers the installed madVR Debug-OSD and reset-statistics shortcut
  bindings at madVR renderer-build time without duplicate VP configuration.
- VP invokes the discovered action through an in-process madVR interface, never
  through global keyboard injection or focus manipulation.
- A changed madVR shortcut is recognized after the next madVR renderer build.
- Missing/unsupported interfaces or unresolved settings disable only this
  optional feature and are clearly logged.
- VP and madVR OSD visibility remain independent.
- x64 Release build succeeds and the verification cases pass without input,
  queue, latency, or rendering regressions.
