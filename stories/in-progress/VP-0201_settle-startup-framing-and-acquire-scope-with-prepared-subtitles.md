# VP-0201: Settle startup framing and acquire scope with prepared subtitles

## Status

In Progress (2026-10-22). Investigating scope/full-raster flashes before startup framing settles, and scope acquisition waiting for a subtitle-free scene despite successful reconstruction/relocation.

## Readiness review

GitHub default and latest beta both resolve to `v1.4.02-beta`. User AGENTS.md explicitly selects the discovered current beta, superseding the older index confirmation gate. Fetched baseline `3746d59e` in clean worktree `E:\codex\videoprocessor\ar-startup-subtitle-acquisition-20261022`, branch `codex/ar-startup-subtitle-acquisition-20261022`. No configuration change or deployment required.

Successful composition precedes qualified subtitle projection, crop evaluation/admission, selected-source submission and startup readiness publication. Startup reveal currently accepts one ready snapshot, including caption FIT that can immediately change. Current runtime logs (host `27dd5ef3`) repeatedly report prepared subtitles rejected with `physical-base-mismatch`: display base 374..1784, sampled/composed picture 383..1776 (later 384/385..1776). The 8-row UHD guard omits the independent sampler's existing 3-row reconciliation tolerance. Prepared presentation must also acquire without falsely changing native detector evidence.

## Required behavior

- Briefly wait for consistent submitted startup geometry, within the existing three-second fallback and current-frame requirement.
- Permit current successfully prepared subtitles to acquire presentation of an independently trusted native scope base without subtitle absence.
- Keep proof tied to exact source/display/policy/continuity context, preserve raw detector evidence and outside-erasure pixel checks.
- Preserve manual/fixed framing, real aspect transitions and user configuration.

## Validation plan

Add regressions for transient startup poses, repeats, context changes and guarded UHD subtitle acquisition; retain conflict/stale/failed-composition vetoes. Build x64 Release and run native regressions. Live playback remains separate acceptance; do not deploy or restart without a user request.

## Tracker audit

Canonical files and index each contained 219 matching unique IDs; maximum root VP-0200 and registry agreed. Assigned VP-0201; no duplicate IDs, missing rows or count mismatch.
