# VP-0222: Withdraw contradicted pillarbox authority on full-width menus

## Status

In Progress (2026-10-30). User authorized implementation and deployment. Source branch `codex/menu-pillarbox-evidence-20261030`, worktree `E:/codex/videoprocessor/menu-pillarbox-evidence-20261030`, freshly fetched current beta `v1.4.02-beta` at `d8518ef8`. Include the separately tested VP-0194 OSD fix when deploying the paired runtime.

## Problem and evidence

Netflix menu side gutters acquired 136,0-3704,2160 (1.65185, labeled 1.66) despite rejected vertical-axis evidence. Later full-width menu frames remained provisional because of bottom darkness; both deferred and provisional paths retained the obsolete aspect indefinitely. Native capture `analysis-20261030-151313-29244-1` has visible picture in 88-93 percent of old side bands. Unmodified model reproduction retained the old crop for 6000 provisional observations, recovering after two authoritative full-raster observations.

## Acceptance criteria

- Current native broad picture evidence on both excluded side bands invalidates stale pillarbox authority after adjacent source confirmation; uncertain vertical boundaries cannot preserve disproved horizontal bars.
- Live and queued state invalidate together, and unavailable measurements never fabricate a replacement ratio.
- Reject incomplete-axis pillarbox acquisition without changing supported ratios or the confirmation latency for genuine measured pillarbox pictures.
- Cover native captured menus, genuine 1.66 and 4:3, real aspect changes, darkness, localized overlays, source repetition/gaps/generation changes, and queued publication.
- Build x64 Release, run applicable native and renderer regressions, back up and deploy the host/renderer pair, verify hashes and startup. Preserve user configuration.

## Tracker audit

Before allocation: 239 canonical files and 239 index rows, no duplicate or missing IDs; highest root 0221. Existing unrelated VP-0207 asset edit preserved. Pre-existing VP-0211 index/state mismatch and noncanonical Status formatting on VP-0070/0088/0168 are outside this change.
