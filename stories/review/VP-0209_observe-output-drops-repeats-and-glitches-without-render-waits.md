# VP-0209: Observe output drops, repeats and glitches without render waits

## Status
Review

2026-10-24: Implementation and qualification complete for review on source commit 7dc9af666b6ca1e004601fee10a4b6d5c10dfe41, pushed to codex/display-statistics-20261024. Matching x64 Release host, both renderers and isolated observer built successfully from the clean commit; 278 focused native tests passed. User's installed player and configuration remain unchanged. Integrated candidate playback, including mismatched-refresh Smooth Motion, remains required before rollout/merge; no visual or physical-scanout acceptance is claimed.

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

## Implementation and qualification - 2026-10-24
The isolated helper uses pinned MIT PresentMon v2.1.0 (de71bba2fb65482f9cf6486284c9eed6aa87698f). Renderer handoff is bounded, allocation-free and nonblocking; correlation, tracing, process management and parsing run outside the render thread. Collection does not change presentation mode, Present flags, cadence or GPU processing. Lost/ambiguous evidence becomes partial/unavailable coverage, not a fabricated dropped frame.

OSD separates unexpected discarded outputs from conservative excess-hold repeat/glitch lower bounds. HDMI input resync and manual Ctrl+Shift+R share a reset boundary that rejects delayed pre-reset evidence. Tests cover cadence, reset races, event gaps/reordering, full queues, stale/failed collection and the actual helper's startup/reset/forced-failure cleanup. Reusable runner tools/test_display_statistics.ps1 passed its 98-test subset, including the real helper lifecycle; these are included in the 278 total, not additional unique tests.

The final producer benchmark measured 61.7/70.3/74.2 ns per submission at p50/p95/p99 of batch averages; a full queue immediately rejected measurement submissions. A four-window off/on/on/off collector-only comparison measured mean helper CPU 0.535% of one core and about 9.1 MB working set, with zero ETW losses or internal overflow. Existing VP CPU/GPU observations showed no measurable uplift in this short run, but workload changed and this was not the integrated new host or 24-to-60 playback. Details, hashes and limits: [qualification receipt](../assets/VP-0209/qualification-20261024.md).

Source worktree: E:\codex\videoprocessor\display-statistics-20261024. Started from discovered remote beta v1.4.02-beta at b1415da4, then fast-forwarded the existing pending PR197 chain through 26e33fdc for reset support. Candidate remains separate, unmerged and undeployed; PR #197 remains draft and unchanged. Raw logs, traces and build/test receipts are outside Git at E:\codex\diagnostics\vp-display-statistics-20261024.
