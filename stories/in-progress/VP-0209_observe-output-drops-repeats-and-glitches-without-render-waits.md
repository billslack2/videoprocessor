# VP-0209: Observe output drops, repeats and glitches without render waits

## Status
In Progress

2026-10-24: User authorized implementation and Luna xhigh research. Design coordination is active; production integration depends on VP-0210 establishing a usable passive display-event source. Current remote beta v1.4.02-beta at b1415da4; clean source worktree E:\codex\videoprocessor\display-statistics-20261024, branch codex/display-statistics-20261024, fast-forwarded the existing pending PR197 chain through 26e33fdc (reset support). No new production source edits or deployment yet.

## Problem
VP's capture/queue losses and cadence drift prediction do not measure displayed drops/repeats/glitches. Existing DXGI polling reports unsupported on the current playback path; missing observations cannot be counted as dropped frames or clean zeroes.

## Design requirements
- Observe output outcomes without changing swapchain mode, present flags, cadence, GPU work, subtitle/AR/HDR behavior or source processing.
- Keep collector startup, ETW parsing, correlation and publication off the render thread. Render-side handoff must be bounded, allocation-free and nonblocking; pressure loses measurement coverage, never playback.
- Correlate immutable process/run/swapchain/output intent with confirmed displayed or discarded outcomes. Never infer discarded output merely from a skipped poll/event ID.
- Account for ordinary 24-to-60 cadence and Smooth Motion outputs. Distinguish observed excess holds from exact repeat totals when presentation phase or refresh coverage is incomplete.
- Preserve truthful unavailable/stale/lost-event states and explicit measurement scope. Count incidents once; recovery cannot erase them.
- Reuse VP-0207's HDMI input resync and configurable Ctrl+Shift+R reset boundary. Delayed old events cannot restore cleared counters.
- No auto-elevation, service installation, persistent system tracing changes, or desktop overlays for collection.

## Acceptance
- Tested passive source for the actual presentation model, process and swapchain correlation, reset/run retirement, unsupported/lost/late/duplicate/out-of-order events, wrap/overflow, normal cadence, Smooth Motion, and injected missed output.
- Adversarial queue/worker stall proves renderer never waits; paired measurement-disabled/enabled CPU and presentation-cadence qualification reports measured overhead and limits.
- Matching x64 Release host and both renderers built; native and actual adapter integration checks recorded. No claim of physical-display/visual acceptance from synthetic tests alone.
- Authoritative source and upstream license/version pinned; PR remains unmerged pending applicable acceptance.

## Dependencies
- VP-0210 passive collector feasibility and overhead spike.
- VP-0207 current output observations and reset implementation at 26e33fdc.

## Implementation progress - 2026-10-24
Collector feasibility gate passed: pinned MIT PresentMon v2.1.0 (de71bba2) compiles with v142/C++14 without a service. A bounded 20-second passive probe observed the deployed VP video swapchain in composed-copy mode, with zero reported ETW event/buffer losses and orderly trace cleanup. Pilot helper CPU 0.109 seconds over 21.49 seconds and sampled working set 9.05 MB; no paired baseline, so this does not prove no playback impact.

Implementation now adds an isolated helper, immutable render intent with exact Present-call QPC span, bounded SPSC handoff, worker-side event matching/reset retention and honest partial OSD totals. Production helper instruments its otherwise unreported internal completed-event ring overflow. Source remains in the recorded worktree; build/regressions and independent review underway. Current deployed player/configuration unchanged. Repeat/glitch counters are conservative excess-hold lower bounds because there is no immutable scheduler-owned absolute vblank target; confirmed discarded outcomes are distinct from missing events.
