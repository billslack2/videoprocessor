# VP-0077: VP-0066 merged-beta acceptance validation

## Status

Review (2026-08-02). VP-0066 and its completed implementation children are
closed based on the stable baseline, x64 Release build/test evidence, merge to
`origin/v1.1.015-beta`, and prior live validation. This record owns the final
user-facing beta acceptance pass and may reopen a referenced story if a
specific regression is reproduced.

## Purpose

As a VideoProcessor user, I want one compact validation record for the merged
live-output-pipeline baseline, so I can assess normal use without reopening or
re-reading the architecture and component-extraction stories.

This is validation only. It authorizes no additional refactor, queue-policy,
timing, or renderer change unless a reproduced result identifies a concrete
regression and the appropriate closed story is reopened.

## Required acceptance pass

1. **Normal playback:** Exercise 23.976 and 59.94 material. Confirm steady VP
   queue, normal latency, and no sustained drop/repeat behavior.
2. **Reset/restart:** Use manual `R` and renderer restart. Confirm clean
   recovery without reset loop, stale frame, prolonged backlog, or stuck UI.
3. **Renderer handoff:** Switch Alpha <-> madVR. Confirm no hang, starvation,
   unexpected latency increase, or retained presentation content.
4. **Output transitions:** Exercise refresh/display and HDR/SDR changes.
   Confirm one bounded re-prime and no repeated black flash/rebuild loop.
5. **Slow HDMI/projector path:** Exercise the slow display/HDMI resync case.
   Confirm a bounded VP reserve rather than a variable/oversized backlog.
6. **Input/source disruption:** Perform a channel/source change or brief
   capture disruption. Confirm current-epoch frames resume and no persistent
   empty downstream queue or stalled UI remains.
7. **Presentation retarget:** Exercise windowed/fullscreen retarget. Confirm
   no crash, stale frame, or timing regression.

For a failure, retain the relevant current log from
`C:\Videoprocessor\vp\logs\vp_debug.log` or numbered rotation and identify
the scenario, renderer, refresh family, and whether manual `R` changes the
result. Reopen the narrow source story rather than altering VP-0077.

## References

- VP-0066: live video output architecture and baseline roll-up.
- VP-0066-3: epoch-aware transport and processing.
- VP-0066-4: DirectShow delivery and lifecycle integration.
- VP-0066-6: output-readiness and deterministic prefill.
- VP-0066-9: one-time fresh-epoch VP queue convergence.
- VP-0065: stale-frame transition invalidation, if retained content returns.

## Acceptance criteria

- The listed scenarios are exercised on the merged beta with no reproducible
  regression requiring changes to the VP-0066 baseline.
- Any failure is linked to a specific log/reproduction and reopened or tracked
  as a separate, narrowly scoped story.
- On acceptance, mark this record Done; no source or configuration changes are
  required solely to close it.
