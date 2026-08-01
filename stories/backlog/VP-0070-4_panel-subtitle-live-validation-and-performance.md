# VP-0070-4: Panel subtitle live validation and performance

## Status

Backlog. Depends on VP-0070-3.

## Parent

[VP-0070](VP-0070_alpha-panel-bound-subtitle-capture-and-relocation.md)

## Scope

Validate the completed panel-only treatment path on representative Apple TV
captures and establish its safety and latency evidence against VP-0066.

## Acceptance criteria

- Corpus evidence covers the parent validation matrix and retains captures,
  state transitions, geometry, and cost metrics.
- Report cue acquisition/release, box stability, false treatments, and any
  safe pass-through decisions.
- Long 60 Hz A/B runs demonstrate no sustained added drop, queue growth, or
  low-latency regression with the feature enabled.
- The input contract and unsupported cases are documented for users.

