# VP-0018: Re-select the content refresh rate when switching to alpha

## Status

Done. Accepted 2026-07-25 after user validation confirmed the renderer-switch
fix works. Implementation commit: `fc3cd35` (`codex/vp0018-v1.1.014`, based on
`v1.1.014-beta`). The fix defers alpha's refresh-rate decision until after its
D3D11 swapchain initialization, so the decision observes any desktop-rate
restore caused by swapchain setup.

Validation: a full `Release|x64` solution rebuild passed for the GUI, tests,
and libplacebo plugin. The resulting `VideoProcessor-GUI.exe` and
`VideoProcessorLibplacebo.dll` were deployed as
`C:\Videoprocessor\vp\VideoProcessor.exe` and
`C:\Videoprocessor\vp\libplacebo\VideoProcessorLibplacebo.dll`; SHA-256
checks confirmed both deployed files match the build outputs. The user
confirmed the change was merged and working.

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

## Implementation plan

Once the matching alpha-renderer source is available, implement and review the
following in one renderer-transition change set:

1. Map the renderer shortcut path and alpha lifecycle callbacks from outgoing
   teardown through incoming construction, swapchain creation, and the first
   display-mode notification. Add a monotonically increasing renderer
   generation at the transition boundary, and discard a deferred request if
   its generation is no longer active.
2. Centralize alpha refresh selection in one idempotent transition owner. The
   outgoing-alpha restore may request the desktop/menu mode; the incoming
   alpha instance may request the content mode only after its display path is
   ready. Neither path may infer actual mode from the other request's cache.
3. Split refresh state into explicit values: desired content target, last
   requested target, Windows target-path mode, verified current Windows mode,
   and measured DXGI rate with its freshness/stability. Restrict the
   "already correct" outcome to a fresh Windows-mode query within the existing
   rate tolerance; target-path and prior-request values are diagnostics only.
4. On alpha initialization, schedule exactly one generation-bound
   re-evaluation after the swapchain/display path becomes ready. Re-query the
   Windows mode before deciding. Apply the content rate if it differs; on a
   verified match, record a no-op. Do not retry from ordinary timing samples.
5. On the ensuing display notification, invalidate stale measurements,
   re-query the actual mode, and emit one post-transition result. Permit one
   bounded retry only when the display path was unavailable or the mode query
   failed; cancel it on a newer renderer generation or renderer destruction.
6. Preserve the existing startup and established-renderer paths. Add focused
   unit seams for rate comparison, generation invalidation, and decision
   selection where the source architecture permits, then run the manual matrix
   below against the deployed Windows display.

### Readiness gate and handoff

The implementation owner needs the exact source repository/worktree that
produces `C:\Videoprocessor\vp\libplacebo\VideoProcessorLibplacebo.dll`, its
revision/build command, and the configuration/API location that performs
Windows mode selection. On handoff, capture a fresh ten-toggle trace with the
new generation/decision fields and attach only the concise excerpts to this
story; keep the full runtime log at `C:\logs\vp_debug.log`.

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
