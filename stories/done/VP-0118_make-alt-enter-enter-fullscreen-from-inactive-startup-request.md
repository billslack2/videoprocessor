# VP-0118: Make Alt+Enter enter fullscreen from an inactive startup request

## Status

Done. Reproduced on deployed `v1.2.001-beta` commit `b2e1956`
on 2026-08-11. With Start fullscreen configured and no visible fullscreen host,
Alt+Enter logged:

```text
Fullscreen session toggle: requested_before=1 active=0
action=cancel-pending requested_after=0
```

The shortcut therefore cancelled the saved/startup request instead of bringing
the application fullscreen. The failure was observed while testing the
Windowed fullscreen presentation choice.

## Progress

- 2026-08-11: Implemented on `codex/vp-0118-fullscreen-toggle-state` at
  `243ce86`, layered on the validated VP-0117 changes. Toggle resolution now
  uses visible host state plus a proven enter/exit transition direction; the
  saved startup request alone no longer counts as a cancellable transition.
- Added the exact `requested=true, active=false, transition=none -> Enter`
  regression assertion and expanded logging to distinguish configured
  preference, session request, visible host state, and transition direction.
- Clean x64 Release build succeeded. The focused regression test passed. The
  complete native suite passed 794/799; the five failures are unrelated
  configuration inventory/profile-fixture tests and reproduced on two runs.
- Packaged for live validation at `VP0118-test-243ce86` with Start fullscreen
  and Windowed fullscreen enabled.
- Live Alt+Enter validation from the focused video preview confirmed keyboard
  receipt, accelerator consumption, fullscreen entry, and fullscreen exit
  without a button event. The operator accepted VP-0118 as complete.
- Merged with VP-0117 to the default `v1.2.001-beta` branch in `58c5b6d` on
  2026-08-11 after a successful merged x64 Release build; all five focused
  VP-0117/VP-0118 regression tests passed.
- 2026-08-12 AltGr follow-up: deployed `b7de6a2` logs proved that Windows
  reported Right Alt+Enter as `ctrl=1 alt=1`, so the exact Alt-only
  accelerator rejected it and MFC subsequently treated the unconsumed Enter
  as the dialog default action, restarting the renderer. Implemented the
  narrow fix on `codex/vp-0118-altgr-fullscreen` at `aa12e0a`: exact
  accelerators retain priority, Right Alt's synthetic Ctrl is ignored only for
  the fullscreen command, and unmatched modified Enter is consumed rather
  than reaching the dialog default. A clean x64 Release build, four focused
  shortcut tests, and all 34 Config UI tests passed. The matched 55-file test
  release was packaged and deployed without changing the active config.

## User story

As a VideoProcessor operator, I want Alt+Enter to enter fullscreen whenever VP
is currently not fullscreen, regardless of the saved startup checkbox or the
selected fullscreen implementation, so the standard shortcut always matches
what is visibly on screen.

## Root-cause boundary

`ResolveFullscreenToggle(fullscreenRequested, fullscreenActive)` treats every
`requested=true, active=false` state as a cancellable pending transition. The
requested bit can also represent a saved/startup default or stale desired
state, so it is not sufficient proof that a real enter transition is pending.

## Scope

1. Separate configured startup preference, current session intent, actual
   fullscreen-host visibility, and a genuinely in-flight enter/exit transition.
2. When no fullscreen host is active and no enter transition is genuinely in
   flight, Alt+Enter must request entry even if Start fullscreen was configured
   or a prior desired-state bit remains set.
3. Preserve deliberate coalescing during a real asynchronous renderer retarget:
   a second shortcut may reverse/cancel that operation only when the transition
   coordinator proves one is active.
4. Apply the same visible-state contract to VP Renderer and DirectShow/madVR,
   and to windowed-fullscreen and the other supported fullscreen choice.
5. Keep the configuration checkbox a next-start preference. Alt+Enter changes
   only the running session and must not rewrite the config file.

## Acceptance criteria

- From an ordinary visible window with Start fullscreen saved as either true
  or false, one Alt+Enter press enters the selected fullscreen mode.
- The exact regression state `requested=true, active=false, transition=false`
  resolves to Enter, not Cancel pending.
- From active fullscreen, one Alt+Enter press exits to the normal window.
- During a proven pending enter or exit, repeated presses coalesce to the final
  visible intent without duplicate renderer rebuilds, a black stranded host,
  or a permanently disabled fullscreen control.
- Behavior is covered for VP Renderer and DirectShow/madVR with both fullscreen
  mode selections, including startup, renderer switch, Config apply, and
  minimized/Video Only combinations where supported.
- Logs distinguish configured preference, session request, actual host state,
  transition direction, and chosen toggle action.
- Focused state-machine tests cover every requested/active/pending combination;
  a clean x64 Release build and live Alt+Enter validation pass.

## Non-goals

- Persisting Alt+Enter changes back to configuration.
- Redefining fullscreen monitor selection or target-only display topology.
- Removing covered renderer-retarget transitions that are still required for
  safe DirectShow or VP Renderer handoff.

## Dependencies and references

- VP-0060: stable fullscreen target ownership and transition latency.
- VP-0094: configured fullscreen-monitor selection.
- VP-0103: safe live configuration publication and session-state retention.

