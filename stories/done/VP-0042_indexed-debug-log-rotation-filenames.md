# VP-0042: Indexed debug-log rotation filenames

## Status

Done. Merged to `v1.1.014-beta` as commit `0954731`.

## User story

As a VideoProcessor user reviewing diagnostics, I want rotated debug logs to
have stable numeric names, so their age and order are obvious without parsing
timestamps.

## Requested layout

The active log remains:

```text
logs\\vp_debug.log
```

Rotated archives use numeric indexes:

```text
logs\\vp_debug.log.0   # newest archived log
logs\\vp_debug.log.1
logs\\vp_debug.log.2
...
```

The current timestamped archive names, such as
`vp_debug.20260728-152412.log`, must no longer be created.

## Required behavior

1. Keep `vp_debug.log` as the active file.
2. Define `.0` as the most recently rotated archive; increasing indexes are
   older archives.
3. Preserve the existing `debug_log_retention` meaning: it is the total number
   of VP debug log files, including the active `vp_debug.log`.
   - Retention `10` therefore keeps `vp_debug.log` plus `.0` through `.8`.
   - Retention `1` keeps only the active file and removes no active output.
4. On rotation, close/flush the active log, discard the oldest indexed archive,
   shift retained archives upward, then rename the previous active log to
   `.0` before opening a new active log.
5. Use exact filename matching. Rotation/pruning may affect only
   `vp_debug.log` and archives matching `vp_debug.log.<non-negative integer>`.
   It must not delete other `.log`, `.tmp`, user, or application files.
6. Rotation remains non-fatal. If a rename, delete, directory, sharing, or
   permission operation fails, VP must continue with the best safe logging
   behavior and emit one actionable diagnostic when possible.
7. Do not overwrite a live active log from another VP process. Define and test
   safe collision behavior for concurrent process startup or an externally
   locked archive.
8. At startup, safely recognize legacy timestamp archives created by the old
   rotation scheme. Decide and document whether they are retained until normal
   age-out or pruned to the configured total. Do not misidentify unrelated
   files as legacy archives.

## Required diagnostics and documentation

- Log the resolved retention count and indexed rotation convention once at
  startup when logging is available.
- Log rotation success/failure with the source/destination names and error
  code, without logging per normal write.
- Update the sample configuration and HTML help to show the indexed layout and
  retain the existing `debug_log_retention` semantics.

## Verification

1. Unit-test retention values `1`, `2`, `10`, and the configured maximum.
2. Test repeated rotations and assert newest-to-oldest ordering is `.0`, `.1`,
   and so on, with no gaps after a successful rotation.
3. Test exact pruning boundaries: active plus exactly `retention - 1` indexed
   archives remain.
4. Test missing directories, permission failures, rename collisions, locked
   files, and concurrent-startup behavior without data loss or crash.
5. Test coexistence with legacy timestamp archives and unrelated similarly
   named files.
6. Confirm no new timestamped archive name is created and existing log
   retention/configuration tests continue to pass.

## Acceptance criteria

- New installations and new rotations produce only `vp_debug.log` and indexed
  archives in the requested format.
- `.0` is reliably the newest archived session and higher indexes are older.
- Configured retention, safe failure handling, and unrelated-file protection
  remain correct.
- Configuration and HTML documentation accurately describe the layout.

## Dependencies

Builds on the completed fixed rotation engine in VP-0030 and configurable
retention behavior in VP-0031.
