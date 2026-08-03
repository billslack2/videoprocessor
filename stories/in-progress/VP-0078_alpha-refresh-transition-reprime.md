# VP-0078: Re-prime Alpha after a real output refresh transition

## Status

In Progress. Began 2026-08-02 on `codex/vp-0078-alpha-refresh-transition`,
based on confirmed current integration branch `origin/v1.1.015-beta`
(`881f6f9b8417082394a092ef02bb050269138ace`), in worktree
`C:\Users\bslac\vp\worktrees\vp-0078-alpha-refresh-transition`. Created
2026-08-02 from the deployed Alpha incident recorded in
`C:\Videoprocessor\vp\logs\vp_debug.log`. This is Alpha-only work. It must
not change the DirectShow/madVR lifecycle, queue policy, or renderer selection
path. The first implementation should re-use the existing configurable
post-renderer-change reset delay used for madVR (verified key:
`/reset_after_render_restart_seconds`; default: five seconds), rather than
introduce a new Alpha-only timer or setting.

Initial implementation audit: the current generic display-transition handler
uses a graph reset and is therefore unsuitable for this Alpha-only operation.
The Alpha renderer already provides `ResetLiveQueue()`; implementation will
add a once-only refresh-transition owner that calls this Alpha-native path
after the shared delay, while retaining VP-0074 as an independent backstop.

Implementation progress (2026-08-02): commit `025f17d` adds a distinct
`refresh-transition` reset reason. A real Windows display change records the
previous/current configured target rates and only arms this path for a
cross-family change (at least 1%; therefore 59.94/60 and 23.976/24 remain
same-family). When the replacement Alpha renderer reaches Rendering, the
shared `/reset_after_render_restart_seconds` delay (default five seconds)
subsumes the existing 30-second display fallback and calls only
`ResetLiveQueue()`. It never calls the DirectShow graph-reset path. A later
same-family/cancelled display notification clears the pending Alpha action;
reset arbitration preserves the higher priority VP-0074 liveness recovery.

Validation: `MSBuild VideoProcessor.sln /m /p:Configuration=Release;Platform=x64`
completed successfully with zero warnings/errors. The x64 Release
`RendererResetPolicyTests` run passed all seven tests, including
`RefreshTransitionReplacesDelayedDisplayFallback`. Live Apple TV transition,
rapid coalescing/cancellation, HDR/LLDV, and Alpha-to-madVR validation remain
before review.

Rebase/deployment (2026-08-02): the confirmed integration branch advanced to
`86981b3` (`feat(alpha): support DeckLink R12B fallback`), so VP-0078 was
rebased cleanly and is now commit `49b7218` on
`codex/vp-0078-alpha-refresh-transition`. A fresh x64 Release build completed
successfully and the focused x64 `RendererResetPolicyTests` run again passed
7/7. Deployed only `VideoProcessor.exe` and
`vprenderer/VideoProcessorVPRenderer.dll` to `C:\Videoprocessor\vp`; their
SHA-256 hashes match the release build. Active configuration and shaders were
unchanged. Recoverable pre-deployment backups are
`VideoProcessor.exe.before-VP0078-rebase-20260802-203100.bak` and
`vprenderer/VideoProcessorVPRenderer.dll.before-VP0078-rebase-20260802-203100.bak`.

Fullscreen-host extension (2026-08-02): live Alpha evidence at 20:42:32
showed a clean target reserve of two frames grow to five frames (about 74 ms
oldest age) after the final windowed-to-fullscreen renderer reconstruction.
The VP-0074 backstop correctly treated that as below its emergency threshold,
then trimmed it only when it reached seven frames/107 ms at 20:42:54. Commit
`a174477` (rebased as `8eea070` onto integration tip `f64a6b6`) adds an
Alpha-only `host-transition` request for fullscreen and
fullscreen-presentation-mode changes. It coalesces rapid toggles into one
queue-only `ResetLiveQueue()` after the existing
`/reset_after_render_restart_seconds` delay; a concurrent refresh transition
shares that one request. The new focused x64 Release policy suite passed 8/8.

madVR audit (2026-08-02): no behavioral change is recommended. Its supported
fullscreen path already performs a serialized `graph-retarget`, followed by
exactly one five-second LiveQueue reset without attempting to infer madVR's
private occupancy. Recorded traces at 15:12:29/15:12:34 and
16:08:06/16:08:11 show the configured second phase both armed and completed.
Keyboard/API toggles during an already-pending madVR retarget could be
hardened by a separately designed coalescing request, but normal UI controls
are disabled during that interval and no incident evidence warrants changing
the mature path as part of VP-0078.

User-directed madVR fullscreen hardening (2026-08-02): commit `35061d3`, on
the latest confirmed integration tip `8b8d900`, now coalesces keyboard/API
fullscreen commands received while a madVR graph-retarget is pending. It never
interrupts or replaces that in-flight graph transaction: if the final checkbox
intent again matches the active target, the retarget reveals normally; if it is
opposite, the existing first-current-frame boundary keeps both hosts covered
and schedules exactly one renderer rebuild toward the final state. This does
not alter madVR's five-second queue re-prime, infer its private queue depth, or
change normal UI-control disabling. The focused x64 Release policy suite now
passes 10/10, including both enter and exit intent symmetry, and a full x64
Release solution build completed with zero warnings/errors.

Latest validation/deployment status (2026-08-02): the rebase to integration
tip `8b8d900` and the madVR coalescing hardening were validated by a full x64
Release build with zero warnings/errors and the focused policy suite (10/10).
Deployment is ready but deferred: the active
`C:\Videoprocessor\vp\VideoProcessor.exe` process (PID 50456) holds both the
executable and Alpha plugin DLL; Windows denied this session's normal close and
forced-stop requests. The prior deployed binaries were backed up successfully
before the copy attempt, and no active binary, configuration, or shader was
changed by that attempt. Resume deployment after the user exits VideoProcessor.

DirectShow-to-Alpha handoff extension (2026-08-02): user testing found that a
madVR-to-Alpha switch was classified as a fresh Alpha queue and therefore
skipped the expected re-prime, leaving three queued frames rather than the
one-frame steady result after a reset. The VP-0078 branch was rebased onto the
open `codex/vp-0080-alpha-crop-failsafe` branch. The new Alpha-only handoff
marker records a DirectShow-to-Alpha renderer replacement at construction and,
when Alpha reaches Rendering, schedules exactly one queue-only
`PostRendererStart` reset after the existing
`/reset_after_render_restart_seconds` delay. It does not inspect queue depth,
rebuild the retired madVR graph, or affect Alpha-to-Alpha or
DirectShow-to-DirectShow starts. Focused x64 Release policy coverage is 11/11,
including the backend-direction matrix.

## User story

As an Alpha-renderer user, I want a menu-to-content refresh transition (for
example Apple TV 59.94 Hz to Netflix 23.976 Hz) to discard transition-era
frames and establish the normal small queue at the new output mode, so it does
not retain even a small, sticky amount of extra live latency.

## Incident evidence

The current deployed log captures two instances of the vulnerable order. The
important 23.976 Hz case is at 18:41:10, with the same ordering repeated at
18:41:48:

```text
18:41:10 | libplacebo refresh-rate switch applied: input=23.976024 Hz
           target=23.976024 Hz previous=59.951000 Hz actual=59.951000 Hz
18:41:10 | Alpha queue generation armed: generation=1 reason=start
           prefill_target=2 hard_capacity=32
18:41:10 | Post-start reset skipped: renderer=VideoProcessor Renderer (Alpha)
           backend=Alpha reason=fresh-queue-and-swapchain
18:41:10 | Output readiness state: ... expected=59.951000Hz
           observed=0.000000Hz ... state=output-not-ready
18:41:10 | Alpha queue startup prefill released: generation=1 depth=2 target=2
```

At 18:41:48 the switch again requested 23.976024 Hz while `actual` and the
Windows target path were still 59.951000 Hz. The measurement observer was
reset to `warming/no measured samples`, but the two-frame Alpha prefill was
released immediately and Alpha again skipped the post-start reset. Therefore
the log does **not** prove a large queue overflow. It shows a smaller gap:
frames and presentation pacing may be admitted under the old output mode and
survive into the new one, while the renderer has no one-shot delayed
refresh-transition re-prime to remove that debt.

This differs from the completed [VP-0074](../done/VP-0074_alpha-latency-resilience-and-NLS-shader-cold-start-recovery.md).
VP-0074 is a backstop for a measured render stall or a queue/age excursion
above the target. It deliberately does not fire for the small two-frame
transition case. This story owns the known output-mode boundary before such a
backlog is allowed to become sticky.

## Problem statement

Alpha currently treats a newly created swapchain and empty queue as sufficient
reason to skip a post-start reset. A refresh-rate request is asynchronous:
the request can be accepted while `QueryDisplayConfig` and DXGI timing still
describe the old 59.95-Hz output. This permits startup prefill and presentation
state to straddle a true 59.94/60 <-> 23.976/24 output transition.

The solution must be an Alpha-native, bounded **transition re-prime** using
the same one-shot post-change delay policy already proven for madVR. The delay
is armed by a known, material Alpha refresh-target change, not by queue depth,
rate-estimate jitter, or a recurring timer. It is not a generic DirectShow
graph reset. A small queue depth is a valid steady state; neither depth zero
nor depth above target alone authorizes this operation.

## Required behavior

1. When Alpha requests a materially different output refresh family, create a
   uniquely identified pending output-transition epoch and arm the existing
   configurable post-renderer-change delay exactly once. Capture the request
   identity, renderer/swapchain generation, input rate, previous configured
   output rate, requested target, and current observed DXGI rate. Do not create
   an epoch for normal sub-ppm measurement updates, a request in the same
   refresh family, or a mere OSD/scene state update.
2. During that pending epoch, mark any Alpha queue/presentation prefill as
   provisional. Preserve prompt first-picture behavior, but do not let a
   provisional reserve become permanent proof that this output mode has been
   primed.
3. The expiry of that one-shot delay is the normal scheduling boundary for the
   re-prime; it must not wait for the longer DXGI rate-accuracy window. At the
   deadline, perform exactly one Alpha-native re-prime for the latest,
   unsuperseded transition identity: close/serialize frame admission as
   needed, discard transition-era queue entries and retained presentation
   content, reset Alpha presentation/cadence/queue-generation bookkeeping, and
   build exactly the configured target reserve from current frames. The stale
   frame shield may remain black only for the bounded re-prime interval.
4. Output evidence remains a diagnostic and safety input, with the exact
   source and confidence recorded separately:

   - configured Windows target-path/display-change evidence establishes that
     the requested family was applied; and
   - fresh DXGI cadence evidence establishes that the observable presentation
     family has changed when it becomes available.

   The implementation review must document the actual configured/DXGI state at
   the deadline, but lack of final DXGI accuracy is not by itself a reason to
   defer the once-only clean-up. A transition that has been explicitly
   cancelled or superseded before the deadline must not consume a re-prime.
5. Do not restart the DirectShow graph or invoke its reset path for this
   Alpha-only operation. Do not reconstruct the Alpha renderer unless code
   review proves queue/presentation invalidation alone cannot establish correct
   D3D11/libplacebo state. Any such escalation requires a separate recorded
   reason and still must be once per transition identity.
6. Coalesce a new target request before the deadline into a single latest
   transition identity; cancellation or a return to the original family
   cancels it. Expiry consumes the final identity once. Ordinary display-rate
   warming, duplicate notifications, or later DXGI evidence must never re-arm
   it or create a periodic reset loop.
7. Keep VP-0074's rate-scaled queue/age recovery as a separate hard-backstop.
   Its recovery must not double-fire with this re-prime; each log record must
   identify whether the action was `refresh-transition`, `stall/age-backstop`,
   manual, or another existing lifecycle action.
8. Preserve current behavior for madVR, non-Alpha renderer transitions,
   same-family channel changes, manually configured refresh overrides, HDR/SDR
   and LLDV metadata transitions, color-output state, NLS, and the existing
   first-picture/black-cover behavior.

## Instrumentation and diagnostics

Add compact, rate-limited transition records containing:

- transition identity and state (`requested`, `timer-armed`, `configured`,
  `DXGI-observed`, `re-primed`, `cancelled`, or `superseded`), including the
  configured delay and actual elapsed delay;
- renderer, swapchain, queue, presentation, and display-measurement
  generations; input/previous/requested/configured/DXGI rates and refresh
  families; and the evidence source/time used for the decision;
- queue target and raw/converted/Alpha depths, oldest-frame age, retained
  presentation presence, provisional-frame count, discarded-frame count, and
  pre-/post-re-prime latency telemetry; and
- the exact reason a re-prime was performed, skipped, deferred, or suppressed,
  including the already-consumed transition identity if applicable.

Do not log every frame or expose unverified physical display latency as a
measured result. A brief Alpha OSD transition state is optional only if it
uses already-published diagnostics and does not change the normal OSD layout.

## Implementation and test plan

1. Audit the existing madVR post-renderer-change timer/reset path, identify
   its configuration key and default, and map the Alpha refresh-switch,
   startup prefill, presentation, black-cover, and queue-recovery call graph.
   Identify one serialized owner for transition identity and one safe
   renderer-thread handoff; preserve all existing thread ownership and lock
   ordering.
2. Implement the transition state machine as a deterministic, unit-testable
   policy with explicit request, coalescing, timer expiry, cancellation, and
   once-only consumption. Cover rate-family classification and prove that
   jitter around a nominal rate cannot produce a new transition or periodic
   timer.
3. Implement Alpha queue/presentation invalidation behind that policy. Re-use
   accepted Alpha queue-generation/first-live-frame mechanisms where safe, but
   do not call DirectShow `Reset` or add a new buffering stage.
4. Add native replay tests for the log sequence above: 59.951 current output,
   23.976 request accepted, observer invalidated, provisional prefill, then
   the configured timer expiry. Prove one re-prime, current-frame reserve at
   the configured depth, continuous safety of the presentation clock, and no
   repeated reset while later samples warm up.
5. Add negative tests for same-family requests, 59.94 channel changes without
   an output refresh change, duplicate display notifications, a cancelled
   request, late confirmation after fallback, queue depth zero/one, and an
   unrelated VP-0074 high-age recovery. Prove each path makes at most one
   owner-specific corrective action.
6. Live validate on the Apple TV path in both directions (59.94 menu ->
   23.976 Netflix and return), at least one HDR/LLDV case, rapid back-to-back
   content changes, manual Alpha restart, and Alpha-to-madVR handoff. Compare
   post-transition Alpha queue age/latency against a clean startup at the same
   rate. Retain the relevant `vp_debug.log` excerpt with the story review
   result.

## Acceptance criteria

- A materially different 59.94/60 <-> 23.976/24 Alpha refresh request arms
  one existing-delay transition re-prime; the final non-superseded request
  consumes exactly one re-prime and returns the Alpha queue to its configured
  reserve using current content.
- No transition-era frame persists after the new output mode is confirmed, and
  no unnecessary renderer/DirectShow restart, repeat reset, or sustained black
  interval occurs.
- The small latency increase seen in the incident converges to the clean
  same-rate Alpha baseline rather than staying sticky; measurement reports the
  queue/presentation evidence rather than claiming physical capture-to-photon
  latency.
- VP-0074 still handles a genuine long render stall, but neither recovery path
  duplicates the other's action.
- Regression coverage proves that normal low queue depths, cadence-measurement
  warm-up, same-family changes, and madVR are unaffected.

## Dependencies and related work

- [VP-0074](../done/VP-0074_alpha-latency-resilience-and-NLS-shader-cold-start-recovery.md)
  (Done): queue/age emergency recovery; this story owns the earlier known
  refresh boundary.
- [VP-0055](../done/VP-0055_display-rate-outlier-quarantine-and-transition-warmup.md)
  (Done): display-rate measurement invalidation/warm-up must remain a
  measurement concern, not an unbounded reset trigger.
- [VP-0066-6](../done/VP-0066-6_output-readiness-and-deterministic-prefill.md)
  and [VP-0066-9](../done/VP-0066-9_fresh-epoch-vp-queue-convergence.md)
  (Done): preserve their DirectShow-only readiness/epoch contract.
- [VP-0077](../review/VP-0077_vp0066-beta-acceptance-validation.md)
  (Review): record any relevant regression result there, but do not block this
  Alpha-specific design on unrelated madVR acceptance.

## Non-goals

- No recurring or queue-depth-driven timer reset; only the existing one-shot
  renderer-transition delay may schedule this Alpha action.
- No attempt to infer or control madVR's private queues.
- No refresh-rate selection redesign, display-rate accuracy retuning, or
  physical latency claim.
- No new user configuration option unless live validation proves a genuinely
  necessary policy choice.
