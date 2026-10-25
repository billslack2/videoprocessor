# VP-0209: Observe output drops, repeats and glitches without render waits

## Status
Review

2026-10-24: Implementation and qualification complete for review on source commit 7dc9af666b6ca1e004601fee10a4b6d5c10dfe41, pushed to codex/display-statistics-20261024. Matching x64 Release host, both renderers and isolated observer built successfully from the clean commit; 278 focused native tests passed. User authorized test deployment after review; deployed at 21:43 EDT and verified as documented below. Configuration is unchanged. Integrated visual playback acceptance, including mismatched-refresh Smooth Motion, remains required before merge; no visual or physical-scanout acceptance is claimed.

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

## Authorized test deployment - 2026-10-24 21:43 EDT
User asked to deploy the ready candidate. Gracefully stopped the previous player and deployed the exact verified x64 Release host, both renderers, both observer copies, runtime sidecars and MIT license files from 7dc9af666b6ca1e004601fee10a4b6d5c10dfe41. Dependency DLL hashes already matched; no dependency or configuration edits were needed. Deployment does not merge the candidate or PR #197.

Rollback backup: C:\Videoprocessor\vp\backups\display-statistics-20261024-214333 (previous target files, existence/hash manifest, original active configuration and pre-deployment log). Configuration SHA-256 remained 928934B02A995D83713FA0363D64692893F8A81B45D3E70A73EE65DA6B146744 before/after restart.

New VP PID 56520 started at 21:43:34 EDT and was responsive. Runtime log confirms clean host and renderer commit 7dc9af66 and both plugin API33 loads. The beta observer child PID33220 started from the deployed beta folder with the VP parent identity; its owned ETW session was active. Every deployed payload hash matches the tested artifact. Startup identity and tracing are verified; user-visible output and mismatched-refresh Smooth Motion acceptance remain pending.

External receipts: E:\codex\diagnostics\vp-display-statistics-20261024\deployment-result.json, deployment-runtime-verification.json, deployed-observer-session.txt and deployment-startup-tail.txt. Repeat/glitch coverage limitations still apply.

## Visible-playback statistics report and diagnostic follow-up - 2026-10-24 22:10 EDT
User reported fullscreen OSD with 33,364 outcomes, 1,842 coverage gaps, zero drops and one retained excessive-hold interval, labelled stale/unavailable. User explicitly confirmed video was visible and no visible playback issue was noticed. Do not equate evidence gaps with dropped frames or infer a rendering defect from this screenshot alone.

Saved log at screenshot time (~21:53:39, VFrames ~35,796) has the separate DXGI observer still successful: 34,679 verified intervals, 544 gaps, zero long holds/excess refreshes. This neither validates nor disproves the passive observer's historical one hold. Source/display rates differ slightly (~59.9411/59.9506 Hz); recurring ~106-second two-refresh gaps are consistent with clock drift, not established overload (render time ~4-5 ms at those events).

Fullscreen exit and renderer recreation occurred at 21:53:42. The later bounded 25-second probe therefore describes a different windowed generation, not the screenshot: 1,500 matching PID/TID/swapchain events, 1,499 unknown-mode/discarded/no-screen-timestamp/no-flags records and one startup lost record; 26 health records report zero loss/overflow. Present starts remain ~16.6846 ms apart. Pinned PresentMon can produce such records without display evidence; current normalization conservatively avoids counting them as actual dropped outputs. Independent Luna review confirmed these distinctions; original fullscreen staleness remains unexplained without observer-specific logs.

Implemented and pushed source b171e828363fb27aa15d328d755c4575528d62ad on codex/display-evidence-health-20261024 in E:\codex\videoprocessor\display-evidence-health-20261024. Created from freshly verified remote beta b1415da4 and fast-forwarded the prior tested candidate 7dc9af66. Changes only clarify OSD evidence reasons, add bounded observer/initialization diagnostics (once per ten seconds), and retain the latest exact hold witness through gaps until reset. Multiple incidents can coalesce into one latest witness with a cumulative count. No render, Present, pacing, source/subtitle/HDR or counter-classification changes; no plugin ABI change. Independent review found no counter-semantic change.

Clean x64 Release host/both renderers/helper built at 22:07:06 EDT; all 280 focused native regressions passed at 22:07:36 EDT, including new health-label and hold-witness retention/reset tests. Runtime-record hashes and x64 PE identities verified. Raw probe, preserved original log/screenshot, build/test/hash receipts remain outside Git at E:\codex\diagnostics\vp-display-statistics-20261024\reported-stale-20261024.

Follow-up test deployment attempted at 22:08 EDT but blocked on the first EXE copy: VP had been independently restarted at 22:06:55 as PID37000 with inaccessible executable metadata, and held the binary open. A graceful close request did not exit within ten seconds. Verified every target hash against the pre-attempt backup: no installed payload changed. User asked to close VP and reply so installation can finish. Prior 7dc9af66 remains installed. Backup: C:\Videoprocessor\vp\backups\display-evidence-health-20261024-220817. No configuration edits. Follow-up remains unmerged and undeployed; same-fullscreen replay with new diagnostics and visual acceptance are pending. PR #197 unchanged.
