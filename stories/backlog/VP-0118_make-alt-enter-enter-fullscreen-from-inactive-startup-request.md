# VP-0118: Make Alt+Enter enter fullscreen from an inactive startup request

## Status

Backlog regression. Reproduced on deployed `v1.2.001-beta` commit `b2e1956`
on 2026-08-11. With Start fullscreen configured and no visible fullscreen host,
Alt+Enter logged:

```text
Fullscreen session toggle: requested_before=1 active=0
action=cancel-pending requested_after=0
```

The shortcut therefore cancelled the saved/startup request instead of bringing
the application fullscreen. The failure was observed while testing the
Windowed fullscreen presentation choice.

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

