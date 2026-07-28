# VP-0031: Make debug-log retention configurable

## Status

In progress on `codex/vp-0030-0031-debug-log-retention`, based on the current
default integration branch `v1.1.014-beta`. The branch includes VP-0030 as the
required fixed-retention engine before adding configuration.

## User story

As a VideoProcessor user, I want to choose how many debug logs VP retains so I
can balance disk usage against the amount of diagnostic history available.

## Scope

Extend the completed fixed log-rotation behavior from VP-0030 with one
documented configuration value. The no-configuration behavior must remain the
same fixed default of ten total log files, including the active log.

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
