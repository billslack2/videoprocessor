# VP-0210: Qualify passive presentation event collection

## Status
Backlog

2026-10-24: Bounded non-production research/build spike authorized as prerequisite for VP-0209. Luna xhigh agents reviewed mpv, MPC Video Renderer, libplacebo, Microsoft DXGI and PresentMon. Current phase is inspecting/compiling a pinned minimal PresentMon collector in E:\codex\diagnostics\vp-display-statistics-20261024\presentmon-spike. No runtime or tracing changes.

## Unknown
Can a maintainable passive collector obtain process/swapchain-specific displayed/discarded outcomes for VP's active windowed presentation path with acceptably low overhead, no render-thread work/waits, and truthful event-loss reporting? Current GetFrameStatistics returns DXGI_ERROR_UNSUPPORTED. Global DWM timing and successful render/submission are insufficient.

## Scope and evidence
Inspect pinned primary source and license; compile a minimal standalone helper or collector without service installation. Disable unrelated GPU tracking. Prove identity, timestamps, outcome semantics, loss counters, ownership/stop behavior, and bounded CPU/memory/handoff design. Never modify output presentation solely to obtain statistics. No privilege/group changes or auto-elevation. Retain scratch source/probe receipts outside the source repository until the dependency decision is made.

## Decision gate
Proceed with VP-0209 only after the collector builds and its available evidence/limitations are explicit. If Windows denies observation or emits incomplete evidence, require unavailable/partial OSD states; do not substitute estimated totals. Paired runtime overhead and actual playback-path coverage remain required before rollout.

## Unblocks
VP-0209.
