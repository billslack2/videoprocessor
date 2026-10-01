# VP-0195: Diagnose available display modes and refresh transitions

## Status

In Progress (2026-10-01). Implementing on `codex/refresh-mode-diagnostics`
in `E:\codex\videoprocessor\custom-display-mode-investigation-20260930`.
GitHub default/latest beta is `v1.3.005-beta`, fetched at
`c84863eaa59e03fe218de824c34b7c0220c4fd87`. The user's standing instruction
authorizes that current remote beta as the source base.

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
VP-0088 and VP-0168 use `In progress` capitalization; VP-0168 also has a
folder/index state mismatch. These unrelated records were preserved.
