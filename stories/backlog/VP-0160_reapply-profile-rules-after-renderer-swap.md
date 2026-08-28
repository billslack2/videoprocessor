# VP-0160: Reapply profile rules after every renderer swap

## Status

Backlog (2026-08-28). Story created from the requested renderer-handoff
behavior. The current GitHub default branch was discovered as
`v1.3.003-beta`; implementation awaits the required developer confirmation of
that base before a source branch or worktree is created.

## User story

As a VideoProcessor user, I want automatic renderer profile rules reapplied
after a renderer swap has successfully started its replacement, so the new
renderer begins with profiles selected from the current source and display
state instead of retaining stale manual or previous-renderer selections.

## Required behavior

1. After every completed renderer-family swap, including madVR to Alpha and
   Alpha to madVR, invoke the same profile-rule reset path used by `Shift+Z`.
2. Run the reset only after the successor renderer is running and able to
   accept the resulting profile application.
3. Bind the deferred work to the current renderer transition/generation. A
   stale completion from a retired renderer or superseded transition must not
   reset profiles on a newer renderer.
4. Reuse the existing `Shift+Z` command/action implementation rather than
   duplicating profile-reset state changes or rule evaluation.
5. Do not run the reset when successor startup fails, and do not introduce a
   second renderer swap/rebuild solely to perform the reset.
6. Emit enough existing or focused diagnostic evidence to identify the swap,
   successor generation, and automatic-rule reapply result.

## Readiness review

- The intended user-visible operation is already defined by the `Shift+Z`
  profile-rule reset path; implementation should call that semantic action.
- The renderer lifecycle already exposes a successful successor-start
  boundary and generation/transition identity. The implementation must attach
  to that boundary rather than using an arbitrary delay.
- The change owns only post-swap profile selection. It does not change rule
  syntax, profile persistence policy, renderer construction, or retirement.
- Focused lifecycle/action tests can prove ordering, exactly-once behavior,
  and stale-generation rejection before the x64 Release build and live
  deployment check.
- Source work will begin in a clean worktree at the confirmed current remote
  beta tip.

## Acceptance criteria

1. A successful madVR to Alpha swap reapplies automatic profile rules through
   the same semantic action as `Shift+Z`, after Alpha reports running.
2. A successful Alpha to madVR swap does the same after madVR reports running.
3. The action runs once per completed renderer swap and never before the
   successor is running.
4. Failed, cancelled, stale, or superseded transitions cannot reset the active
   renderer's profiles.
5. The implementation reuses the existing rules-reset command path and does
   not duplicate its state mutation or evaluation logic.
6. Focused automated tests cover both swap directions, failed startup, and a
   stale/superseded completion; the relevant full suite and clean x64 Release
   build pass.
7. Deployment replaces the matched Release `VideoProcessor.exe` and
   `vprenderer\VideoProcessorVPRenderer.dll` pair from one commit, preserves
   active configuration, and verifies deployed hashes against the build
   artifacts.

## Non-goals

- Changing what `Shift+Z` means or changing profile-rule precedence.
- Reapplying rules before the successor renderer is ready.
- Resetting profiles on unrelated viewport, display, or same-renderer updates
  that do not constitute a renderer swap.
- Editing or replacing the user's active configuration.

## Dependencies and references

- VP-0028: unified renderer profiles and the profile-rule reset action.
- VP-0134: renderer handoff lifecycle and generation safety.
- VP-0152: coordinated profile selection actions and feedback.

