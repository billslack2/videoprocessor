# VP-0204: Bound render-thread diagnostics and picture-proof work

## Status

In Progress

Implementation starts 2026-10-22 on `codex/render-thread-jitter-20261022` in
`E:\codex\videoprocessor\render-thread-jitter-20261022`, based on verified
remote beta `v1.4.02-beta` at `fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`.
Readiness and scope below are recorded before implementation. Root owns picture
evidence/cache/deadline integration, the principal engineer owns logging, and
the threading architect owns NVAPI telemetry isolation. No implementation/test
success is claimed yet. Separate LUT PR #191 is not changed.

Created 2026-10-22 as the separately authorized implementation of findings
2 through 5 from the render-thread jitter review. No deployment is authorized.

## Objective and scope

Reduce avoidable render-thread stalls without weakening current picture,
subtitle, crop, resource-lifetime, or presentation-safety decisions. Implement
the following four findings together in one separate reviewable PR:

1. Logging: remove writer file flush/I/O from the producer queue critical
   section; keep producer work and queue growth bounded and expose dropped or
   contended diagnostics without recursively logging from the logger.
2. NVAPI telemetry: prevent slow vendor/driver sampling from holding a shared
   telemetry mutex that a render/UI consumer needs. Publish short-lived result
   snapshots while respecting vendor API and worker lifecycle requirements.
3. Optional picture-proof deadlines: honor the available deadline around
   expensive evidence work, return unavailable/deferred on exhaustion, and
   never promote partial work to authoritative picture or subtitle proof.
4. Duplicate pixel measurements: reuse exact immutable per-frame evidence
   across compatible preview/live requests instead of scanning the same pixels
   again. Keys and invalidation must distinguish source/frame/format and all
   measurement-affecting policy or geometry inputs.

## Boundaries and related work

- [VP-0203](../review/VP-0203_load-calibration-lut-disk-changes-only-on-explicit-config-apply.md)
  and its [PR #191](https://github.com/billslack2/videoprocessor/pull/191) remain
  untouched. LUT Apply-only behavior is not included in this branch or PR.
- [VP-0124](../in-progress/VP-0124_safe-outward-active-picture-lookahead.md)
  records the older outward-lookahead feature investigation. This task bounds
  the existing beta's work and preserves its decisions; it does not reopen
  feature enablement or change crop policy.
- [VP-0140](../review/VP-0140_compile-vp-renderer-shaders-without-blocking-ui-or-presentation.md)
  owns shader preparation. Shader compilation architecture, screenshot readback,
  new GPU-context owners, capture transport, and broad renderer extraction are
  outside this task.
- No deployment, player restart, configuration/LUT edits, or acceptance of live
  playback timing is implied by software implementation and automated tests.

## Readiness and source identity

The root session verified current remote/default beta `v1.4.02-beta` at
`fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`. The new source worktree was verified
clean at that exact commit before implementation:
`E:\codex\videoprocessor\render-thread-jitter-20261022`, branch
`codex/render-thread-jitter-20261022`. Existing source worktrees and LUT PR #191
are preserved.

The static audit identifies producer/writer queue-lock coupling, shared NVAPI
sampling-lock coupling, deadline-insensitive evidence helpers, and duplicate
per-pixel evidence requests. The implementation keeps existing owner-thread
resource lifetimes, uses bounded immutable telemetry/evidence publication, and
returns safe unavailable results when optional work cannot finish. Independent
principal-engineer and threading-architect reviews cover ownership and tests.
No new library or capture hardware API is required.

## Runtime evidence and measurement limits

The reviewed deployed logs identify host/renderer `6e53a6e`, which is not an
ancestor of source baseline `fcc33fc`; those measurements motivate work and are
not current-branch performance acceptance. On 2026-10-22 at 19:11:46,
`C:\Videoprocessor\vp\logs\vp.log.0` records 48/48 preview attempts incomplete
and overdue, average 2.758 ms / peak 3.567 ms, with unknown presentation phase
and a 1 ms fallback allowance. Actual 24 fps scanout drops are not established
by the display-period counter. The same log has 117,594 lines over 4,881 seconds;
logger traffic is real, but logger or NVAPI causation of a visible stall has not
been measured. Keep software/runtime attribution explicit.

## Acceptance and validation

1. Deterministically hold or delay a writer flush and prove producer queue work
   makes bounded progress without waiting for file I/O; preserve shutdown,
   ordering/retention, external sink, and observable overflow behavior.
2. Delay the vendor sampling operation in a test seam and show consumers obtain
   a coherent latest snapshot without waiting for that operation. Test start,
   stop, empty/failed sample, and publication/lifecycle races as appropriate.
3. Exhaust deadlines before and within expensive optional proof; assert that
   no partial evidence becomes authority and mandatory current-frame safety
   decisions retain their baseline semantics.
4. Verify evidence reuse for the exact same immutable frame and cache misses
   for changed frame/sequence/generation/format or relevant measurement inputs.
   Test success, unavailable, cancellation, reset, and worker/live handoff.
5. Use representative captured or synthetic SDR/HDR, planar/native-format,
   letterbox/full-raster, near-black, subtitle/OSD, and changing-geometry fixtures
   where relevant. Compare output/decisions and actual scan/work counts, rather
   than tests that only duplicate implementation structure.
6. Record appropriate focused regression results, a successful x64 Release
   build, exact source identity, and independent review. Any broader existing
   failures must be distinguished with baseline reproduction where needed.
7. Add low-cost counters or sampled timing for deadline deferral/overrun,
   evidence reuse/miss/work, and telemetry/producer contention where useful.
   Avoid new per-frame text or synchronous I/O. Report performance fixtures and
   measurements honestly; no improved live jitter claim without runtime data.

## Tracker audit

Fetched origin/main `08c2595c726bcf2841188c12411befbc05fd16bc` contained
222 canonical records and index rows: 201 roots, 21 children, highest root
VP-0203. Registry Last VP-0203 / Next VP-0204 / count 222 matched. No duplicate,
missing, orphan, or non-contiguous child IDs were found. No open canonical story
covers this four-finding scope. Historical VP-0070/VP-0088/VP-0168 status-format
exceptions remain reported and untouched. This task does not modify VP-0203.
