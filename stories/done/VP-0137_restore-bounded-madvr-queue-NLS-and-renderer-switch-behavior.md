# VP-0137: Restore bounded madVR queue, NLS, and renderer-switch behavior

## Status

Done (2026-08-19). The developer accepted the deployed x64 Release build at
source commit `44c8a86` as "much better" after live madVR/NLS and rapid
renderer-switch testing. The seven-commit repair series on
`codex/vp-madvr-handshake-queue`, based on the discovered default integration
branch `v1.2.001-beta` at `bb8f5c1`, is:

- `8a299f2` Prevent NLS geometry changes from resetting madVR.
- `9bbb502` Recover persistent queue backlog and serialize renderer switches.
- `7e2d731` Restore madVR NLS pre-resize execution.
- `9600276` Prevent NLS delivery gate from starving graph reset.
- `8005370` Restore deterministic queue and NLS transitions.
- `3fa9828` Bound steady raw queue to one live handoff.
- `44c8a86` Release madVR OSD on graph owner during teardown.

The series was merged and published to `origin/v1.2.001-beta` as merge commit
`f572a618b651b71b8692b61f88f70e851f0751d9`; the feature branch is retained at
`origin/codex/vp-madvr-handshake-queue` with tip `44c8a86`.

## User story

As a madVR operator, I want HDMI/content transitions, NLS toggles, manual graph
resets, and renderer changes to converge to the intended bounded live queue
without permanent latency, stutter, or an unresponsive retiring renderer.

## Reported regressions

- Switching Apple TV menu to YouTube TV at the same 59.94 Hz could leave raw
  and converted queues elevated indefinitely, adding roughly 290 ms of latency
  and putting audio out of sync until a manual reset.
- Enabling madVR NLS could fail to fill madVR's presentation queues, stutter
  badly, and remain degraded after NLS was disabled.
- Graph reset and rapid VP Renderer/madVR switching could leave the UI and
  shutdown path locked with `renderer_retirement_pending=1`.
- madVR NLS configuration had regressed from the required pre-resize shader
  stage, and geometry-only changes could request unnecessary graph resets.

## Root causes and repair

1. Post-convergence capture admission allowed multiple raw samples to remain
   queued behind conversion/delivery. Steady live ingress is now a one-sample,
   latest-wins handoff while deterministic startup and transition prefill
   remain separately governed.
2. NLS shader/aspect changes contended indefinitely with synchronous madVR
   delivery and reset work. Delivery admission is now bounded and explicitly
   quiesced for the serialized shader transaction/reset boundary.
3. NLS transition logic mixed geometry changes, graph-reset policy, and queue
   epoch recovery. The repair restores the simpler timed convergence behavior,
   avoids reset for compatible geometry-only changes, and preserves a bounded
   recovery path for a genuinely persistent backlog.
4. `IMadVROsdServices` could survive graph-thread teardown and be released
   later on the retirement MTA during object destruction. madVR now clears its
   bitmap and releases the OSD COM interface on the graph owner before the
   renderer/filter graph is released, eliminating that retirement lock.
5. The active madVR NLS rule is restored to pre-resize execution, matching the
   previously working geometry contract.

## Acceptance criteria

- Same-rate source changes converge without a persistent raw/converted backlog
  or sustained added A/V latency.
- Enabling and disabling madVR NLS preserves presentation-queue fill and
  returns to the non-NLS steady state without a manual reset.
- Compatible NLS geometry changes do not rebuild or reset the graph.
- A graph reset cannot wait indefinitely behind madVR delivery or an NLS
  shader transaction.
- Rapid renderer switching and process shutdown retire the outgoing graph and
  native madVR OSD without a UI, close, or retirement deadlock.
- The x64 Release host and VP Renderer DLL build and deploy as one matched
  artifact pair; the full native test suite passes.

## Validation and release evidence

- Clean x64 Release build completed successfully at `44c8a86`.
- The merged Release host, VP Renderer DLL, and native-test DLL rebuilt from
  `f572a61`; the host's generated version identity names that merge commit.
- Complete native suite passed 863/863.
- All 57 staged deployment files were hash-verified after copying; the active
  configuration hash remained
  `1C68C551C4480E696D92185D170CEF854B8643FC951EC9AD251C3025F6E2BE76`.
- The prior deployed pair is recoverable under
  `C:\Videoprocessor\vp\backups\20260819-134055-renderer-retirement-44c8a86`.
- Live validation covered the reported same-rate transition, madVR NLS
  enable/disable behavior, graph reset, and rapid renderer switching. The
  developer accepted the final result on 2026-08-19.

## Relationship to other work

- VP-0061 and VP-0084 track broader DirectShow reset/queue investigations.
- VP-0106 reduced madVR NLS command latency but explicitly left synchronous
  delivery stalls unresolved; this story fixes the reproduced transaction and
  recovery failure.
- VP-0134 remains the broader display-state and symmetric handoff effort. This
  bug fix closes the concrete graph/OSD retirement lock without claiming that
  story's full display-state validation matrix.
