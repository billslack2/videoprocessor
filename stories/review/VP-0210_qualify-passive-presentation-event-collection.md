# VP-0210: Qualify passive presentation event collection

## Status
Review

2026-10-24: Feasibility gate passed and the collector was adopted by VP-0209 at source commit 7dc9af666b6ca1e004601fee10a4b6d5c10dfe41. Luna xhigh research reviewed mpv, MPC Video Renderer, libplacebo, Microsoft DXGI and PresentMon. Pinned MIT PresentMon v2.1.0 provides usable partial output evidence on the active composed-copy path. Paired collector-only overhead measurement and real inherited-handle lifecycle tests are complete; integrated candidate playback remains unaccepted. No deployed player/configuration changes.

## Unknown
Can a maintainable passive collector obtain process/swapchain-specific displayed/discarded outcomes for VP's active windowed presentation path with acceptably low overhead, no render-thread work/waits, and truthful event-loss reporting? Current GetFrameStatistics returns DXGI_ERROR_UNSUPPORTED. Global DWM timing and successful render/submission are insufficient.

## Scope and evidence
Inspect pinned primary source and license; compile a minimal standalone helper or collector without service installation. Disable unrelated GPU tracking. Prove identity, timestamps, outcome semantics, loss counters, ownership/stop behavior, and bounded CPU/memory/handoff design. Never modify output presentation solely to obtain statistics. No privilege/group changes or auto-elevation. Retain scratch source/probe receipts outside the source repository until the dependency decision is made.

## Decision gate
Proceed with VP-0209 only after the collector builds and its available evidence/limitations are explicit. If Windows denies observation or emits incomplete evidence, require unavailable/partial OSD states; do not substitute estimated totals. Paired runtime overhead and actual playback-path coverage remain required before rollout.

## Unblocks
VP-0209.

## Feasibility result - 2026-10-24
Pinned v2.1.0 de71bba2fb65482f9cf6486284c9eed6aa87698f PresentData builds unmodified with v142; v2.6.0 required a larger unrelated dependency tree. A minimal helper ran under the current token against confirmed VP PID53756 for 20 seconds: display outcomes were observed for native video swapchain 0x18DA0D1A000/mode5, two startup lost-analysis events were identified, ETW loss counters remained zero, helper exited0 and its session was absent afterward. GPU/input/frame-type tracking disabled. CPU0.109s / elapsed21.49s, peak sampled working set9052160 bytes. No active VP/config edits.

Decision: proceed with isolated v2.1.0 collector, pinned MIT provenance, exact event/intent identity, internal-overflow instrumentation and explicit partial/unavailable states. Production source/helper builds but full integration and paired runtime impact checks remain VP-0209 qualification. Spike is in Review, not physical-display acceptance. Detailed receipts in E:\codex\diagnostics\vp-display-statistics-20261024; research-playback.md records pinned mpv/MPC/libplacebo/Microsoft sources and limits. No claim of exact per-refresh physical scanout totals.

## Completed qualification - 2026-10-24
Four 20-second off/on/on/off windows with the original VP process preserved measured mean helper CPU 0.535% of one core, roughly 9.1 MB working set, zero ETW event/buffer loss and zero internal completed-ring overflow. Both observation windows received 1,198 output events; startup lost-analysis records were identified as unknown, not dropped. Helper exited normally and only its owned trace was retired. VP CPU/GPU observations had no measurable uplift in this short comparison; scene/workload drift prevents a causal performance claim.

VP-0209 additionally tests the production inherited parent/stop-handle path: healthy startup, reset acknowledgement, deliberate termination of only the test's collector, unavailable state, subsequent reset and no leftover child/trace. The finite overhead probe and production lifecycle test cover different mechanisms. The final helper's SHA-256 equals the measured helper; source, host, both renderers and helper identities are recorded in [the qualification receipt](../assets/VP-0209/qualification-20261024.md).

Remaining limits: collector-only matched-refresh playback is not integrated candidate or mismatched-refresh GPU acceptance; repeated/glitch totals are conservative observed excessive-hold lower bounds, not complete physical scanout accounting. Keep in Review for the integration acceptance recorded under VP-0209.

## Post-deployment coverage limitation - 2026-10-24
User reported stale passive evidence during visibly normal fullscreen playback. Separate DXGI observations remained healthy at that screenshot time. A later windowed probe (after renderer recreation) delivered ~60 Hz matching presents and zero ETW loss, but unknown mode and no screen timestamps; it cannot explain the earlier fullscreen failure or prove visible drops. VP-0209 follow-up b171e828 adds precise health/witness diagnostics and passes 280 regressions; deployment awaits closing the active player. Preserve Review: feasibility on one observed presentation path is not reliable coverage of all modes/lifetimes.
