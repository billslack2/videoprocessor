# VP-0031: Make debug-log retention configurable

## Status

Review. Implemented by commit `e016e1f` on
`codex/vp-0030-0031-debug-log-retention`, based on the current default
integration branch `v1.1.014-beta`. The branch includes VP-0030 as the required
fixed-retention engine. Pull request:
https://github.com/billslack2/videoprocessor/pull/14.

## User story

As a VideoProcessor user, I want to choose how many debug logs VP retains so I
can balance disk usage against the amount of diagnostic history available.

## Scope

Extend the completed fixed log-rotation behavior from VP-0030 with one
documented configuration value. The no-configuration behavior must remain the
same fixed default of ten total log files, including the active log.

The log remains at `logs\vp_debug.log` beneath the main executable directory.
VP does not create the `logs` directory; when it is missing or unwritable,
logging is disabled for that process without falling back elsewhere.

## Required design decisions

Before implementation, choose and document:

- the configuration file/section and exact key name;
- allowed integer range and a safe upper limit;
- whether zero means disabled rotation, retain only the active log, or is
  rejected (do not make this ambiguous);
- behavior for invalid, duplicate, or unreadable values;
- whether the setting is read only at startup or may be safely reloaded.

The initial expectation is a startup-only setting with a conservative bounded
range and clear log output showing the resolved retention count.

## Resolved design

- File/section/key: `VideoProcessor.cfg`, `[logging]`,
  `debug_log_retention`.
- Semantics: total VP log files including the active log.
- Allowed range: `1` through `100`; the default is `10`.
- Zero is invalid. It does not disable rotation. A value of `1` retains only
  the active log.
- Omitted, invalid, out-of-range, duplicate, or unreadable values resolve to
  the default of `10`. Duplicate logging warnings are non-fatal; unrelated
  configuration syntax warnings retain their existing strict behavior.
- The setting is read once before logger rotation at process startup and is
  not dynamically reloaded.
- The resolved count and its source/default reason are written once in the
  startup diagnostics when the log destination is writable.

## Implementation and verification

1. Reuse VP-0030's filename matcher and retention engine. Do not duplicate
   delete/prune logic in configuration code.
2. Load and validate the setting before logger rotation runs, defaulting to ten
   on absent or invalid configuration.
3. Log the configured/resolved value once at startup and include it in relevant
   diagnostics.
4. Document the setting and examples in the appropriate configuration and HTML
   help without overwriting user deployment configuration.
5. Add tests for default behavior, valid minimum/normal/maximum values, invalid
   values, and protection of unrelated files at every supported retention count.

## Acceptance criteria

- A documented, validated setting controls VP's log-file retention count.
- Omitted configuration preserves the VP-0030 default of ten total files.
- Invalid values safely resolve to the documented default or rejection behavior.
- Rotation remains non-fatal and cannot delete unrelated files.

## Implementation evidence

- Configuration resolution passes one validated count into the VP-0030 engine;
  configuration code contains no delete or pruning logic.
- The sample configuration and HTML reference document the startup-only
  setting, full allowed range, default/error behavior, exact log location, and
  missing-directory behavior.
- Debug x64 test and GUI projects build with VS 18 MSBuild.
- Twelve focused VS 2019 C++ unit tests pass, including default/valid/invalid
  configuration, schema ownership, missing-directory behavior, failure
  recovery, and every supported retention count from `1` through `100`.
