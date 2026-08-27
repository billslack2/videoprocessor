# VP-0153: Reset queues after every display transition

## Status

Review (2026-08-27). Ported to `codex/vp-0153-display-transition-reset` at
`ddacb2a`, based on `v1.3.001-beta`; the x64 Release
`VideoProcessor-GUI` build completed successfully. A local proof-of-behavior
was previously deployed from equivalent commit `1fa0c2d`, and the operator
confirmed it worked. The branch is pushed for review; draft-PR creation was
denied by GitHub with `403 Resource not accessible by integration`. Review it
at https://github.com/billslack2/videoprocessor/compare/v1.3.001-beta...codex/vp-0153-display-transition-reset?expand=1.
Do not merge or deploy this branch again until the operator approves it.

## User story

As a VideoProcessor operator, I want every confirmed Windows display,
refresh-rate, or HDMI transition to schedule the ordinary queue reset after
the configured delay, so the queue returns to a known-good state after the
larger unavoidable display interruption.

## Required behavior

1. On a display-change notification while a renderer is running, schedule a
   `DisplayTransition` queue reset using `queue_reset_delay_seconds`; do not
   treat it as a 30-second fallback only.
2. Preserve reset-coordinator coalescing: duplicate notifications and a more
   urgent renderer recovery may satisfy or replace the pending request, but a
   normal display transition must never be silently left without recovery.
3. Update diagnostics to say the configured-delay reset was scheduled.
4. Preserve the active queue/profile selection and all configuration, state,
   source, display, and renderer settings.

## Acceptance criteria

- A refresh-rate or HDMI/display reset logs a `DisplayTransition` request with
  exactly the configured delay.
- The request is queue-only for VP Renderer and remains safely coalesced with
  a renderer replacement or higher-priority reset.
- No active configuration or state file is overwritten.
- Focused x64 Release build succeeds.
- The resulting branch is placed in Review for operator approval before any
  PR merge or further deployment.

## Validation evidence to record on completion

- Commit SHA on the current beta-based branch.
- x64 Release build result.
- A debug-log excerpt showing the requested configured-delay reset after a
  display transition, or the coordinator record showing that an equivalent
  higher-priority reset covered it.

## Related work

- VP-0078: Alpha refresh-transition re-prime
- VP-0145: Renderer post-stall reset eligibility

## Tracker audit note

- Created after fetching `origin/main` and auditing 171 canonical records on
  2026-08-27. `VP-0152` was the highest assigned root ID.
