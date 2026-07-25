# VP-0018: Re-select the content refresh rate when switching to alpha

## Status

Draft. The current log contains a reproducible-looking renderer-switch
sequence where alpha starts at the content rate but does not reliably re-apply
that rate after another renderer restores the desktop/movie-menu rate. Begin
implementation only after the refresh-switch ownership and stale-state path
are confirmed from code and a repeatable trace.

## User story

As a user of the alpha renderer, I want switching renderers while content is
playing to select the display refresh rate that matches the current content,
just as startup does and as the established renderer does, so alpha does not
leave a 23.976 Hz movie running at approximately 59.94 Hz after a renderer
transition.

## Observed behavior

Startup generally appears to select the correct rate. The problem is most
visible when switching away from alpha and then back to alpha while the same
content is still active:

1. Alpha is rendering at approximately 23.976 Hz.
2. Switching away restores 59.941 Hz.
3. Switching back to alpha logs that the display is already 23.976 Hz.
4. The newly initialized alpha swapchain then reports a 59 Hz display mode and
   subsequent timing measurements are approximately 59.9 Hz, not 23.976 Hz.

This strongly suggests that the “already correct” decision is using stale or
logical target state rather than a verified current display mode, or that the
switch request occurs before the renderer transition has completed and is not
re-evaluated afterward. It is a hypothesis, not yet a confirmed root cause.

## Evidence from `C:\logs\vp_debug.log`

The full log is the primary reference artifact:

`C:\logs\vp_debug.log`

Relevant excerpts from the 2026-07-25 11:07–11:09 renderer-switch sequence:

```text
11:07:38 | libplacebo refresh-rate switch applied: input=23.976024 Hz target=23.976024 Hz previous=59.941000 Hz actual=23.976000 Hz
11:07:40 | ... selected=23.975502 Hz (measured DXGI)
11:08:59 | Renderer shortcut render.5 selected: D
11:09:00 | Windows display mode changed: 3840 x 2160, 32 bits; display-rate measurement reset
11:09:00 | libplacebo refresh-rate restore applied: 59.941000 Hz
11:09:01 | Shaders: evaluating input signal=HDR refresh=23.976024 Hz nominal=23
11:09:16 | Renderer shortcut render.6 selected: V
11:09:16 | display: restored runtime manual rule 'rec709' for new renderer
11:09:16 | libplacebo refresh-rate switch: display already 23.976000 Hz for 23.976024 Hz input
11:09:16 | libplacebo output negotiation (initialize): ...
11:09:16 | libplacebo DXGI output: ... mode=3840x2160@59Hz bits=32
11:09:16 | Windows display mode changed: 3840 x 2160, 32 bits; display-rate measurement reset
11:09:16 | ... Windows target path=59.941000 Hz ... selected=0.000000 Hz (measured DXGI)
11:09:18 | ... selected=58.960927 Hz ... Windows target path=59.941000 Hz
11:09:28 | ... selected=59.851881 Hz ... Windows target path=59.941000 Hz
11:09:48 | ... selected=59.909367 Hz ... Windows target path=59.941000 Hz
```

The full unmodified file must remain available for correlation with renderer
teardown, display-mode notifications, source-state updates, and any later
manual switch. Do not check a copy of the potentially large runtime log into
the repository unless specifically requested; reference the path above and
retain the snippets in this story.

## Scope

Alpha/libplacebo refresh-rate selection and renderer-switch lifecycle only.
The story covers:

- the content-rate request and current-display comparison;
- Windows display-mode restore performed when leaving alpha;
- alpha teardown/recreation and swapchain initialization;
- display-mode-change notifications and timing-measurement reset;
- re-checking/re-applying the content rate after the new renderer is ready;
- diagnostics and regression tests.

Do not change the established renderer's known-good refresh behavior except
where a shared ownership contract requires a narrowly scoped correction.
Do not “fix” the issue by hard-coding 23.976, by trusting the nominal rate
alone, or by repeatedly switching modes on every timing sample.

## Required investigation

1. Trace the complete state machine from `Renderer shortcut render.5/.6`
   through renderer teardown, display restore, alpha construction, swapchain
   creation, `Windows display mode changed`, and refresh selection.
2. Identify every cached value used in the “display already ...” decision:
   requested target, Windows target-path mode, measured DXGI timing, last mode,
   renderer generation, and any transition/restore flag.
3. Determine whether the first alpha decision runs before the restore/display
   notification has settled. The comparison must not treat a stale target path
   or stale selected rate as proof of the physical/current mode.
4. Establish which component owns mode selection during a renderer transition.
   There must be one idempotent owner and one generation-aware request, not
   competing restore and reselect calls.
5. Confirm the behavior for startup, alpha-to-established, established-to-alpha,
   repeated toggles, menu/SDR transitions, and content-rate changes while the
   same renderer remains selected.

## Proposed behavior

After a renderer switch:

1. Capture the current content cadence and desired nominal/precise refresh.
2. Restore or settle the outgoing renderer's display mode as required.
3. Recreate/initialize the incoming renderer.
4. After the incoming renderer and display path are ready, query the current
   Windows mode and re-evaluate the content-rate request for the new renderer
   generation.
5. Apply the mode only if the verified current mode does not match the target;
   otherwise log a verified no-op.
6. After the mode-change notification, re-query and log requested target,
   Windows target path, measured DXGI rate, and actual display mode.

The comparison must distinguish “requested/target path says 23.976” from
“the current display mode is verified as 23.976.” A stale logical value must
never suppress a necessary switch. The operation must be idempotent and
bounded so mode-change notifications cannot create a switch loop.

## Diagnostics

Add or refine rate-limited logs around each transition with:

- renderer generation and renderer name;
- content rate and nominal bucket;
- requested target mode;
- verified current Windows mode before the decision;
- cached/target-path value, if different;
- decision (`apply`, `verified no-op`, or `defer until renderer ready`);
- display-mode notification and post-notification verification;
- DXGI measured rate, sample stability, and Windows target path;
- failure/fallback reason and retry generation.

The wording must make it obvious when a switch was skipped because the mode
was verified versus skipped because only a stale target-path value was known.

## Verification

- Start alpha with 23.976/24, 25, 29.97/30, 50, and 59.94/60 Hz content and
  verify the selected mode after stabilization.
- Switch alpha → established → alpha during active 23.976 content; repeat at
  least ten times and verify alpha returns to the content rate every time.
- Repeat established → alpha while the menu/SDR state is active, then begin
  movie playback and verify the rate is re-evaluated.
- Switch renderers during an HDR/SDR/LLDV-style transition and after a display
  mode restore. Verify no stale target suppresses the required switch.
- Confirm no repeated mode-switch loop, renderer restart loop, queue drain,
  frame-drop spike, or unnecessary audio-delay command.
- Compare alpha and established-renderer logs for the same content and record
  actual Windows mode, DXGI measurement, and target path separately.
- Preserve correct startup behavior and verify a verified no-op does not issue
  a redundant mode change.

## Acceptance criteria

- Switching to alpha re-checks the actual current display mode after renderer
  creation and applies the content-matching rate when needed.
- The “display already” path is accepted only with a verified current mode, not
  merely a stale target-path/cache value.
- Logs clearly correlate renderer generation, request, decision, display-mode
  notification, and post-switch verification.
- Repeated renderer toggles are stable and do not cause switch loops, drops,
  queue starvation, or avoidable restarts.
- Startup, content transitions, menu/SDR behavior, and the established
  renderer's existing behavior remain correct.
- The complete source log remains referenced at `C:\logs\vp_debug.log`, with
  the captured excerpts above preserved as the initial investigation evidence.
