# VP-0160: Reapply profile rules after every renderer swap

## Status

Review (2026-08-28). Implemented on
`codex/vp-0160-post-swap-rule-reapply` from confirmed base
`v1.3.003-beta` at `a737ecd3`. Source commit `3524e2c0` merged through PR #73
into `v1.3.003-beta` at `0cf07a95`.

The accepted successor renderer now owns a one-shot generation token whenever
its name differs from the last renderer that actually reached running. The
exact successor consumes that token in the accepted `RENDERSTATE_RENDERING`
callback and invokes `OnCommandReapplyRules()`, the existing semantic path used
by `Shift+Z`. Failed, stale, superseded, and same-renderer starts cannot consume
the action. If reapplication itself requires a renderer reconstruction, the
current post-start flow stops and the committed automatic snapshot is carried
into that fresh construction.

Validation and release evidence:

- Focused x64 Release `RendererGenerationGateTests`: 6/6 passed.
- Complete x64 Release test assembly: 950/951 passed. The sole failure is the
  beta baseline's `ConfigurationReferenceMatchesPublicFieldInventory` check;
  it compares unchanged `CONFIGURATION.html`/inventory inputs and is unrelated
  to the four VP-0160 source/test files.
- Clean sequential x64 Release rebuilds of `VideoProcessor-GUI.vcxproj` and
  `VideoProcessor-VPRenderer.vcxproj` passed from merged beta commit
  `0cf07a95`; the executable embedded version reports that clean commit.
- The matched Release pair was deployed to `C:\Videoprocessor\vp` with backup
  `C:\Videoprocessor\vp\backup-before-vp0160-20260828-103726`.
- Deployed `VideoProcessor.exe` SHA-256:
  `40587326EB79BDDB6AF901A578A861166C9C7FA3A244EEA1B33F2BE0F8EF9D17`.
- Deployed `vprenderer\VideoProcessorVPRenderer.dll` SHA-256:
  `30D84016FAE0BE3890206E1F394D5F178B792926032F571A1147A02349AACFE3`.
- Both hashes exactly match their build artifacts. No configuration or state
  file was edited; the active `VideoProcessor.cfg` hash remained
  `41D10C9DD49BB920BED4222D530E0196D092078DA7577AA52E1A0EB069572AA9`.

Remaining acceptance is the live hardware exercise: establish a manual
profile override, switch madVR to Alpha and Alpha to madVR, and confirm each
successor resets to current automatic rules only after it is running. Verify
the `Post-swap profile rules reapply` and `Profile rules re-applied` log entries
for the same generation and confirm a failed/superseded start produces no
reapply entry.

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
