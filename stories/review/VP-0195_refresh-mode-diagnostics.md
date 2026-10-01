# VP-0195: Diagnose available display modes and refresh transitions

## Status

Review (2026-10-01). Implemented and pushed as `8d49537e` on
`codex/refresh-mode-diagnostics`, based on current beta `v1.3.005-beta`
at `c84863eaa59e03fe218de824c34b7c0220c4fd87`.
[Draft PR #125](https://github.com/billslack2/videoprocessor/pull/125).
Source worktree: `E:\codex\videoprocessor\custom-display-mode-investigation-20260930`.
The user's standing instruction authorized the current remote beta as base.

Completed mode inventory, GPU/output identity, rational rates, exclusions,
ranking, before/after desktop and signal snapshots, apply/verify/rollback/restore
diagnostics with correlated IDs. Scaled/interlaced inventory remains diagnostic
only. Hardened changing mode-count handling and unexpected enumeration failures.

Validation: x64 Release host, renderer and test build passed with 0 errors
(6 existing upstream warnings). All 36 selected display refresh tests passed,
including 3 new mode-enumeration tests. A read-only probe compiled from the
production diagnostic functions emitted 416 inventory/snapshot lines on the
local RTX 3080 at 2560x1440; no display mode was changed. `git diff --check`
passed. Evidence lives in the worktree's `refresh-diagnostics-build.log`,
`TestResults/refresh-diagnostics.trx`, and `artifacts/refresh-diagnostics/probe.log`.

Remaining: code review and field validation on the reporting 1080p projector.
No deployment or active configuration edits. The diagnostic guide records
pre-existing risks: omitted scaled modes, rate-only verification, target-only
maximum-mode resolution choice, and lost restore ownership after failed rollback.
Those policy/recovery behaviors remain unchanged and are not established as
the user's cause. Detailed findings: `docs/VP-0195_REFRESH_DIAGNOSTICS.md`.

## Request

A user with a 1920x1080 projector reports refresh switching and picture
geometry problems. The reported 1920x816 is a cropped screen area, not a
custom Windows mode. Add useful mode inventory and switching diagnostics;
review serious issues and risks affecting 1080p. No deployment requested.

## Readiness review

The current renderer uses DXGI rational modes at the current desktop size,
then SetDisplayConfig and bounded readback verification. Existing logs omit
the full inventory, enumeration failures and complete before/after timing.
Instrumentation runs at lifecycle transitions, outside per-frame work. Keep
the selection policy unchanged while making excluded modes and errors visible.
Build x64 Release and run the relevant refresh-policy tests. Hardware behavior
on the reporting user's projector remains a field validation requirement.

## Acceptance criteria

- Log display identity, source rate, current desktop and signal timing.
- Log available modes with exact rational rates, format, scan/scaling flags,
  eligibility, and enumeration errors; expose scaled/interlaced modes for diagnosis.
- Correlate candidates, apply results, observed timing, verification and restore.
- Explain 1080p findings and remaining uncertainty without claiming reproduction.
- Record build/test evidence and keep diagnostic output off the per-frame path.

## Tracker audit

Before allocation: 213 canonical files and table rows; highest root 0194,
next 0195, registry counts and IDs agree. Pre-existing discrepancies:
VP-0088 and VP-0168 use `In progress` capitalization; VP-0168 also uses
that capitalization in the index instead of the canonical `In Progress`. These unrelated records were preserved.
