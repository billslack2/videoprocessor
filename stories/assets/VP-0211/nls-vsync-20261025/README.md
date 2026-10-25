# NLS/every-refresh stutter correction, 2026-10-25

Draft PR #204: https://github.com/billslack2/videoprocessor/pull/204
Isolated fix: fc20e916d2af777cdddc309568072ad032acb6b5, base current beta
8c4537462bbabd6ce07ba3e9ffd9be9bc8745492. Running installation had the separate
PR177 tone GUI test head dedc85d96f2e21c7b7ae3be7e3debd5983b6100a. Preserve it
in combined test source 2a22430849f27fef9cea3a1cca909d94a522b310, remote branch
codex/nls-vsync-tone-test-20261025. The delta from installed dedc85d9 is exactly
the five files in #204; no Config GUI changes or settings migration are added.

The preserved incident shows NLS enabled 13:18:37-13:18:56, every-refresh on,
~59.941 Hz input/output, 122 paired extra repeats/source retirements. 518 selected
PTS values use a 10us lattice; old SourceDue tolerated only 0.1us. Shared
SourceDue now allows max(10us, one capture clock tick), capped 100us. This
addresses a deterministic rounding defect without the rejected half-refresh
cadence phase change. Actual GPU overload is not established by these logs.
Runtime Motion was off; screenshot Always was not the runtime setting.

Validation:
- Candidate Release solution build passed, both renderers, 10 existing warnings.
- 429 selected native/Motion/cache/subtitle tests passed in 7.0 min, including
  full-resolution backing, cleanup and composition GPU tests.
- Exact clean fc20e916 rebuild passed with zero warnings/errors; all 16 clock
  tests passed again. Includes 1us/10us matched source queue cases, real drift,
  original 24-to-60 phase, bounded future eligibility and generation replacement.
- Luna xhigh final review found no remaining concrete blocker. It rejected the
  earlier half-refresh candidate and caught a missing temporal diagnostic argument;
  both were corrected before the final passing builds.
- Clean combined 2a224308 x64 Release solution build: 74 existing warnings,
  zero errors. All 421 focused exact-combined-source regressions passed, including
  actual bundled NLS GPU hook, NLS failure recovery, tone profile compatibility,
  cadence, source reuse and temporal clean-packet checks.
- A supplementary broad combined parser/GPU run was deliberately stopped after
  its key NLS/subtitle recovery and edge cases passed to avoid duplicating the
  completed 429-test suite. Its partial log is preserved; it is not a completed
  suite pass and had no observed failed case. The 421-test selected scope then
  ran to completion. Live subtitle acceptance is still not claimed.

Deployment:
- User's existing feature deployment/start authorization carried through this
  critical regression repair. Matching host + primary/Beta renderer DLLs and
  Release/x64 runtime receipts hash-verified; Config GUI remained installed from
  dedc85d9 and its running session was left alone.
- Backup location and before/after hashes in deployment.json. Config was backed
  up and preserved byte-for-byte: every-refresh remains off and depth 3.
- Initial copy hit a file lock; verified all deployed binary hashes unchanged.
  Graceful WM_CLOSE did not exit PID 20068 within 15s, so only that inspected
  player process was stopped before the successful replacement/restart.
- Restarted responsive PID 54532; fresh host/renderer log identities both match
  clean 2a224308. Restored playback window after inherited minimized startup.
- Live simultaneous-on NLS confirmation remains pending. New periodic repeat
  diagnostics report next_pts_lead_ms, pts_tolerance_us and queued_sources.
  These submission diagnostics do not prove physical scanout or subtitle quality.

Rollback: stop VP, restore VideoProcessor.exe, both renderer DLLs and their
.runtime.json receipts from the backup recorded in deployment.json, then restart
from C:\Videoprocessor\vp. Preserve current configuration unless the user asks
to roll settings back. Earlier failed-attempt backups also remain recoverable.
No merge or public release of #204 was performed. VP-0211 remains Review.
