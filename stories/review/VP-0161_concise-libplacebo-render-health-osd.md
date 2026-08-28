# VP-0161: Concise libplacebo-style renderer health OSD

## Status

Review (2026-08-28). Implemented on branch
`codex/vp-0161-render-health` from confirmed GitHub default
`v1.3.003-beta` at `0cf07a95`; source commit `342327c7` is published to
`billslack2/videoprocessor`.

Alpha's Ctrl+I OSD now replaces the two generic frame-counter rows with six
concise libplacebo-style rows: `Render health`, `Frames rendered`, `Dropped
frames`, `Times stalled`, `Render frame`, and `Submit frame`. Health warms for
eight successful render-and-submit cycles, becomes `Degraded` for ten seconds
after a newly observed renderer drop or frame-rate-aware material stall, and
otherwise reports `Good`. Renderer and capture drops remain explicitly
separate, while intentional scene-aware D/R telemetry remains in its existing
section.

Validation evidence:

- Focused x64 Release `RendererHealthTrackerTests`: 5/5 passed.
- Complete x64 Release test assembly: 955/956 passed. The sole failure is the
  beta baseline's `ConfigurationReferenceMatchesPublicFieldInventory` check;
  it compares unchanged `CONFIGURATION.html`/configuration inventory inputs
  and is unrelated to the ten VP-0161 source/project/test files.
- A clean sequential x64 Release solution build passed at source commit
  `342327c7`; the generated executable identity reported that commit with
  `VERSION_DIRTY=false`.
- The matched Release executable/renderer pair was deployed to
  `C:\Videoprocessor\vp` with backup
  `C:\Videoprocessor\vp\backup-before-vp0161-20260828-175220`.
- Deployed `VideoProcessor.exe` SHA-256:
  `9CD5409B52EC5B346B9236BA9F1AF32BDD61AB47A279234D084FF82DFBB44ECC`.
- Deployed `vprenderer\VideoProcessorVPRenderer.dll` SHA-256:
  `43E6750B01363F1762EC39EA221292ECE6E83D72DECC1011F06EE2EC570379EE`.
- Both deployed hashes exactly match the clean source-commit build artifacts.
  No configuration was edited; the active `VideoProcessor.cfg` SHA-256
  remained
  `DAC456C0D12657A2B4D10569D79D5B4DDCF7FB0816D8C6F70F53965E8FFBD7C3`.

Remaining acceptance is the live Alpha UI exercise: open Ctrl+I during video,
confirm the six rows fit without clipping, verify warm-up settles to `Good`,
and observe that a real renderer drop or material render stall temporarily
shows `Degraded` while preserving cumulative counters.

## User story

As a VideoProcessor operator, I want Ctrl+I to summarize whether the Alpha
renderer is healthy and show the few counters and timings that explain that
status, so I can recognize dropped work, stalls, or inadequate render margin
without reading the debug log or a wall of low-level statistics.

## Required behavior

1. Add one concise Alpha-only `Render health` summary to the existing Ctrl+I
   OSD. It must distinguish warming telemetry, healthy rendering, and a recent
   rendering problem.
2. Use the useful libplacebo demo terms directly: `Frames rendered`, `Dropped
   frames`, `Times stalled`, `Render frame`, and `Submit frame`.
3. Count `Frames rendered` only after both rendering and submission succeed.
4. Keep `Dropped frames` tied to VP's existing renderer-owned discard/failure
   counter. Preserve capture-missed visibility without presenting it as an
   Alpha render failure.
5. Count `Times stalled` only for materially slow render cycles using the
   existing Alpha frame-rate-aware stall threshold. Report cumulative stall
   duration as context.
6. Report compact average/peak milliseconds for successful render and submit
   operations. Do not add demo-only decode, PTS, FBO, interface-draw, sleep, or
   swap rows when VP lacks a useful equivalent.
7. Do not add `Missed timestamps` or repeated-frame counters without a real
   presentation-deadline/evidence contract. The current Alpha FIFO does not
   own such a timestamp scheduler, while intentional scene-aware repeats are
   already reported separately.

## Readiness review

- The existing Alpha path already measures per-frame libplacebo render time,
  swap/present blocking time, successful presents, renderer drops, capture
  misses, and a frame-rate-aware material-stall threshold.
- Ctrl+I already has a renderer-native/fallback shared bitmap path and a
  renderer-neutral stats snapshot. The change can extend those contracts
  without changing presentation ownership, queue behavior, configuration, or
  OSD placement.
- A small renderer-health accumulator can be deterministic and unit tested:
  lifetime counters/average/peak values plus a bounded recent-issue hold for
  the summary state.
- The UI must preserve the previous snapshot when its non-blocking renderer
  read loses the render-lock race, just as existing Alpha telemetry does.
- Source work will use a clean worktree from the confirmed current remote beta
  tip and an x64 Release build.

## Acceptance criteria

1. With Alpha active, Ctrl+I shows no more than six rendering-health rows and
   uses the required labels.
2. Successful render+submit cycles increment `Frames rendered` and update
   average/peak `Render frame` and `Submit frame` timings.
3. Renderer discards/failures remain visible as `Dropped frames`; capture
   misses remain visibly separate context.
4. A render cycle at or above the existing material-stall threshold increments
   `Times stalled`, accumulates its duration, and makes `Render health`
   degraded for a bounded recent interval.
5. A newly observed renderer drop also makes health degraded for the bounded
   interval; healthy operation subsequently returns to `Good` without erasing
   cumulative counters.
6. Before sufficient successful samples exist, health reads `Warming` rather
   than claiming success.
7. Non-Alpha renderers retain their current OSD behavior and no unsupported
   metric is fabricated.
8. Focused unit tests and the relevant full x64 Release build/test targets
   pass from the clean feature worktree.

## Non-goals

- Changing queue depth, cadence correction, frame scheduling, or renderer
  recovery policy.
- Treating intentional scene-aware correction repeats as rendering failures.
- Adding every timing row visible in the libplacebo demo application.
- Editing or replacing deployed configuration.

## References

- libplacebo `plplay` example and its `Frame statistics / GPU timing` panel.
- Microsoft DXGI frame-statistics documentation for the distinction between
  submitted presents and display evidence.
- VP-0024 and VP-0066 for Alpha timing/queue observability foundations.
- VP-0158 for the current native Ctrl+I/profile OSD behavior.
