# VP-0095: Target-only fullscreen display session and recovery

## Status

Merged baseline on `origin/v1.1.016-beta` as `3dc792e` (2026-08-05).
The active deployment configuration remains on `existing` while a follow-up
resolves Windows rejecting the supplied one-path CCD topology on this hardware.

## User story

As a VideoProcessor user with a configured projector that Windows may leave
disabled, I want an explicitly enabled, use-with-caution mode that temporarily
activates only that projector while VP is fullscreen, then restores the exact
pre-session display topology when VP is done.

## Scope

1. Add an opt-in main configuration setting:

   ```ini
   [general]
   fullscreen_monitor_name: EPSON PJ
   fullscreen_monitor_session_mode: target-only
   ```

   Omitted or `existing` preserves VP-0094 behavior and never changes topology.
2. Before renderer/UI startup in `target-only` mode, use native CCD APIs to find
   exactly one connected active or inactive target by the same exact, trimmed,
   case-insensitive friendly-name rule as VP-0094.
3. Capture the current complete display topology before applying any change.
   Persist a versioned recovery transaction in the existing VP state file before
   disabling any path. Include enough identity, path, mode, position, primary,
   and activation information to restore the captured topology exactly.
4. Validate first, then atomically apply a temporary topology in which the
   configured target is the only active display. Do not invoke an external
   display utility and do not save the target-only topology as the user's normal
   Windows topology.
5. Continue normal VP startup only after Windows reports the configured target
   active. If validation, application, or bounded activation confirmation fails,
   restore the snapshot, log the exact failure, and continue using existing
   VP-0094 fallback behavior without disabling other displays.
6. Restore the captured topology when fullscreen is turned off or VP exits.
   If the target was inactive before the session, restoration disables it again;
   every display VP disabled is re-enabled with its captured mode and position.
7. Add a special command-line recovery mode, for example:

   ```text
   VideoProcessor.exe /fix_display
   ```

   It must load the existing state file, restore the pending pre-session display
   transaction, clear it only after confirmed success, and exit without creating
   the VP dialog, capture device, renderer, or fullscreen host. It is idempotent:
   no pending transaction exits successfully without changing displays.
8. On ordinary startup, detect a stale pending transaction left by an abnormal
   prior exit and restore it before beginning a new target-only session.
9. Log snapshot persistence, validation, apply, activation confirmation,
   restoration source (`normal-exit`, `fullscreen-off`, `startup-recovery`, or
   `/fix_display`), Windows error codes, and whether the recovery record cleared.

## Safety and failure behavior

- Mark this feature clearly as use-with-caution in configuration documentation.
- Never select by resolution, display ordinal, `DISPLAYn`, serial, or partial
  friendly name.
- Never alter topology for an omitted, unavailable, disconnected, unsupported,
  or ambiguous configured target.
- Persist recovery state before the first topology mutation and retain it after
  every unsuccessful or unconfirmed restore.
- A hard process/OS failure cannot run in-process cleanup; `/fix_display` and
  next-start stale recovery are the supported recovery paths.
- Do not add NirCmd, MultiMonitorTool, DisplaySwitch, PowerShell, or another
  external display-management dependency.

## Verification

1. Start with Epson disabled and LG active. Confirm VP records LG topology,
   enables only Epson, starts fullscreen there, then restores LG and disables
   Epson on fullscreen-off and normal exit.
2. Start with both displays active. Confirm target-only disables LG and restore
   returns both displays to their exact original modes, positions, and primary.
3. Terminate VP after target-only apply, then run `/fix_display`. Confirm no VP UI
   or renderer launches and the exact original topology is restored.
4. Run `/fix_display` with no pending transaction; confirm a successful no-op.
5. Test missing, disconnected, and duplicate friendly-name targets; no topology
   change may occur.
6. Force validate/apply/restore failures and confirm the transaction remains
   recoverable and diagnostics identify the Windows error.
7. Confirm ordinary VP-0094 behavior is unchanged when session mode is omitted.
8. Complete a clean x64 Release build and the full unit test suite.

## Acceptance criteria

- Target-only mode is explicit, deterministic, native, recoverable, and off by
  default.
- The pre-session topology is durably recorded before mutation and restored
  exactly on normal completion.
- `/fix_display` performs recovery and exits without launching VP.
- Failed or ambiguous selection never disables a working display.

## Implementation and test record

- Source branch: `codex/vp0095-target-only-display-recovery`, based on the
  discovered remote default `origin/v1.1.016-beta`.
- Tested source commit: `c12e7fd`.
- Clean x64 Release rebuild completed with `VERSION_DIRTY=false`.
- Full native test suite passed: 606 of 606.
- Deployed Release pair to `C:\Videoprocessor\vp` after backing up the EXE,
  renderer DLL, active configuration, and state file.
- With both LG and Epson paths initially active, VP persisted an 877-character
  `display_recovery.v1` transaction containing two paths and four modes before
  applying the target-only topology for `EPSON PJ`.
- Normal-exit test restored the captured two-path/four-mode topology and cleared
  the recovery record only after confirmation.
- Controlled abnormal-exit test left the recovery transaction pending;
  `VideoProcessor.exe /fix_display` restored the topology, cleared the record,
  exited with code 0, and left no VP UI/process running.
- `/fix_display` with no pending transaction also exited successfully without
  launching VP or changing state.
- An initial deployment exposed Windows error 5 in the atomic state replacement
  because VP retained its own input stream. Commit `c12e7fd` closes that handle
  before `MoveFileEx`; the fail-safe prevented topology mutation before the fix.
- Live configuration contained an unmerged
  `subtitle_release_drift_seconds` setting from a separate feature deployment.
  Its two values remain preserved as comments for this default-branch VP-0095
  test because `v1.1.016-beta` does not yet recognize that setting.
- Rebased cleanly onto the later `origin/v1.1.016-beta` commit `4d1d3bc`,
  which includes subtitle translation and recognizes that setting. The rebased
  x64 Release build passed the expanded native suite: 619 of 619.
- Deployed the rebased Release EXE/renderer pair on 2026-08-05. The deployed
  build reports `ge9251a2`; its no-op `/fix_display` check exited 0, then the
  normal launch persisted recovery state and activated `EPSON PJ` target-only.
