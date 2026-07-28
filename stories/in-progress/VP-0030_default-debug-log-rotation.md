# VP-0030: Rotate debug logs with a fixed default retention of ten files

## Status

In progress on `codex/vp-0030-0031-debug-log-retention`, based on the current
default integration branch `v1.1.014-beta`.

## User story

As a VideoProcessor user or developer, I want the debug log to rotate
automatically so the current log remains manageable and recent startup/crash
history is preserved without manual file cleanup.

## Scope

Implement fixed log rotation for the shared `DebugLog` facility. The normal
deployment log is currently `C:\logs\vp_debug.log`; the logger also has a
fallback `debug.log` beside the executable when that path cannot be used.

The initial retention policy is fixed in code:

- retain at most **ten log files total**, including the active log;
- on a new VP process/logger initialization, close and archive the prior active
  log with an unambiguous timestamped filename, then create a fresh active log;
- prune only older archives belonging to the same log basename after a new
  active log has been established;
- never delete unrelated `.log` files from the directory.

The fixed count deliberately has no user configuration. VP-0031 is the
separate follow-up for making it configurable.

## Constraints

- Rotation must happen before normal asynchronous logging begins, with no
  producer-thread blocking or loss of the first initialization diagnostics.
- Preserve the existing active log path so support tooling and users can still
  open `C:\logs\vp_debug.log` while VP is running.
- Use a sortable, collision-safe timestamp suffix such as
  `vp_debug.20260727-153045.log`; do not rely solely on filesystem creation
  time for ordering.
- If archive rename, directory enumeration, pruning, or a fallback path fails,
  VP must continue logging to the active file and emit the best available
  diagnostic. Logging failure must never prevent startup.
- Handle a missing log directory, read-only/archive failure, and simultaneous
  stale files from a prior interrupted process safely.
- Do not add configuration parsing, GUI settings, or new command-line options
  in this story.

## Implementation plan

1. Identify the logger initialization/shutdown ownership in
   `src\VideoProcessor-Lib\DebugLog.h` and make file rotation occur exactly
   once for each process logger lifetime.
2. Define a single filename parser/matcher for active and archived VP logs.
   It must accept only logs produced by this feature and reject arbitrary files.
3. Before opening the active file, attempt to archive a nonempty existing active
   log to a timestamped name. Use a deterministic collision suffix if a log was
   already archived during the same timestamp granularity.
4. Open the new active log, then enumerate matched files and delete the oldest
   archives until active plus archives is at most ten files.
5. Add concise diagnostics for rotation success, archive/prune failure, selected
   active path, and retained-file count. Avoid recursive logging while the log
   destination is being initialized.
6. Add focused tests using a temporary directory for first run, normal rotation,
   timestamps/collisions, exactly-ten retention, more-than-ten pruning, empty
   active file, unrelated-file preservation, and recoverable filesystem errors.
7. Update HTML/configuration help only to document the fixed ten-file default;
   do not expose a setting yet.

## Verification

- Start VP repeatedly and confirm the active path remains stable while previous
  runs are archived with sortable names.
- Confirm a directory with twelve matching logs is pruned to ten total files
  and that the newest archives and active file are retained.
- Confirm unrelated log files, other application files, and filenames that only
  resemble VP logs are never deleted.
- Test `C:\logs` and the executable-directory fallback path, including failure
  to archive/prune. VP must still start and log where possible.
- Confirm no deadlock, startup delay, or lost first diagnostics under the async
  logger.

## Acceptance criteria

- VP retains at most ten of its own debug log files by default, including the
  current active log.
- Each normal new run archives the previous active log and starts a fresh one.
- Rotation failures are non-fatal and diagnosable.
- No unrelated file can be deleted by retention pruning.
- The count is intentionally fixed; configuration belongs only to VP-0031.
