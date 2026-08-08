# VP-0070-4: Panel subtitle live validation and performance

## Status

Blocked 2026-08-08. Depends on an accepted VP-0070-3 treatment path. Resume
only when VP-0070-1 through VP-0070-3 have accepted detector, diagnostic, and
same-frame restoration/relocation evidence ready for representative live
validation.

## Parent

[VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

Validate the completed panel-bound treatment path on representative Apple TV
captures in both Alpha and DirectShow/madVR, and establish its safety and
latency evidence against VP-0066.

## Acceptance criteria

- Corpus evidence covers the parent validation matrix and retains captures,
  state transitions, geometry, and cost metrics.
- Report top/bottom crossing and bar-only recall separately, and prove zero
  treatment for picture-only, padding-only, and unstable-boundary cases.
- Report cue acquisition/release, box stability, false treatments, and any
  safe pass-through decisions.
- Compare diagnostics off, classical-only, and PP-OCR-assisted acquisition,
  including cold/P50/P95/P99 inference cost, result age, stale rejection, and
  mailbox replacement counts.
- Long 60 Hz A/B runs demonstrate no sustained added drop, queue growth, or
  low-latency regression with the feature enabled.
- The input contract and unsupported cases are documented for users.
