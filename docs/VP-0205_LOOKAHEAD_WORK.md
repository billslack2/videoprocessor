# VP-0205: Preserve useful lookahead

Lookahead enables crop and caption decisions on their first eligible displayed
source frame. This design preserves configured future-frame depth while moving
more complete proof ahead of presentation and replacing the fixed 2 ms ceiling
with admission based on measured presentation headroom.

## Status and branch scope

Planning draft, 2026-10-23. Runtime implementation has not started. The canonical
[VP-0205 story](https://github.com/billslack2/videoprocessor/blob/main/stories/backlog/VP-0205_preserve-useful-lookahead-with-prepared-proof-and-adaptive-budgets.md)
contains acceptance criteria and validation requirements.

This separate branch starts from the discovered current/default beta
`v1.4.02-beta` at `f7c7a8204f7a2cace68b3b40774406680af6c0d8`.
[PR #192](https://github.com/billslack2/videoprocessor/pull/192) remains a separate
unmerged change. Its cooperative helper cancellation, exact-worker imports and
telemetry APIs must be reconciled against the final accepted version before
runtime implementation. Its logger, NVIDIA and fullscreen work is outside this PR.

## Why the existing limit needs review

The 1 ms unknown-phase allowance and 2 ms qualified-phase ceiling predate PR #192.
That PR enforces the ticket inside expensive scans. A later helper cancellation
can discard observations completed earlier in the call, and consumption then loses
the selected source's first-frame opportunity. Independent raw measurements,
subtitle publication, the outward worker and temporal interpolation continue
outside this synchronous ticket. A completed call can also contain a shortened
ready prefix; completion is not equivalent to full-horizon proof or adoption.

Existing tests show first-frame expansion with sufficient evidence versus three
source intervals later in one conservative fallback fixture (about 125 ms at
24 Hz). They also cover a cached outward proof validating with zero foreground
allowance. The objective is to retain that value while controlling work that can
hold presentation.

## Measured evidence

The preserved capture has these observed synchronous analysis results:

| Source and display rate | Attempts | Completed | Optional deadline overruns |
| --- | ---: | ---: | ---: |
| 23.976 to 23.976 Hz | 16,165 | 13,745 | 2,432 |
| 59.94 to 59.94 Hz | 50,250 | 50,249 | 3 |

Twelve 24 Hz windows contain 576 attempts and zero completions, accumulating about
1.202 s of optional work with no reported source/display-period exceedances.
The retained logs include dropped/coalesced diagnostics, so these observations
cannot supply a complete proof-adoption rate or establish displayed-frame loss.
No measured 24-to-60 Hz performance windows occurred.

At 24 Hz, sampled post-reserve headroom has median 2.368 ms and p95 4.246 ms.
The 2 ms ceiling binds in 286/376 qualified reports. These are rate-limited samples,
not frame percentages. A 41.7 ms source interval does not imply that much remaining
analysis time; arrival phase, output cadence and reserved mandatory work matter.

An isolated x64 Release retention-helper sweep used 15 synthetic owned 4K fixtures,
101 rotated trials per allowance, precomputed raw/global/darkness measurements,
and three evaluations of the same fixture:

| Allowance | Completed trials | Completion |
| --- | ---: | ---: |
| 2 ms | 1356/1515 | 89.5% |
| 3 ms | 1512/1515 | 99.8% |
| 4 ms | 1515/1515 | 100% |

These helper costs demonstrate a completion cliff, not a safe 4 ms full-renderer
budget. Measurement source was integration `e1996d67e42df7ee0ba63e2fc7577a43d132af48`;
this planning branch has no runtime implementation to compare yet.

[Capture aggregates](https://github.com/billslack2/videoprocessor/blob/main/stories/assets/VP-0205/capture-summary.json),
[helper results](https://github.com/billslack2/videoprocessor/blob/main/stories/assets/VP-0205/helper-budget-summary.json)
and [recorded source and limitations](https://github.com/billslack2/videoprocessor/blob/main/stories/assets/VP-0205/measurement-provenance.json)
are preserved with the authoritative story.

## Proposed worker and publication design

Reuse immutable raw/global/darkness measurements across workers and overlapping
windows. The outward worker currently duplicates measurements also computed by the
raw worker. Prepare expensive base-relative proof ahead of consumption using
bounded queues, imminent-source priority, deduplication and obsolete-job rejection.
Keep expensive proof jobs from starving raw or subtitle work; render publication
must never wait for a worker.

Retain independently complete measurements when later helpers cancel. Publish a
crop/caption authority certificate only after its complete contiguous proof
succeeds. Publication must be idempotent, preserve later caption refinement and
avoid duplicate temporal votes. Partial scans cannot become black, clearance or
crop evidence.

Reuse must match exact frame/storage interpretation, retained immutable ownership,
analysis domain, transport/format/viewport/policy/continuity generations, and all
base-relative dependencies: bounds, owner/subtitle context and history revision.
Retain live current-pixel, owner and stable-reference admission checks. Rejection
counts alone do not justify weakening those checks.

## Proposed presentation admission

Allocate synchronous work from actual display phase and a conservative reserve
for mandatory-work tail cost. Distinguish new-source preparation from reused
repeated-output preparation. Define unknown/stale-phase, cadence conversion,
resize/restart and overload behavior. A ticket remains fixed; analysis cannot
move its own target into another display interval merely to finish.

Choose bounds from measured quality and presentation behavior. Replacing 2 ms with
another unexplained universal constant, or using the full source period as a CPU
budget during 24-to-60 Hz conversion, does not meet the objective.

## Telemetry and validation

Measure selected/ready/required depth, ready-prefix loss, stage cost and
cancellation, repeated/discarded complete work, expected no-change/invalid proof,
and deadline lateness. Report valid proof ready before its first eligible frame,
publication, actual adoption/rejection reason and effective source lead. Existing
worker-completed counters omit reasons for invalid proof and cannot establish
useful delivery.

Use deterministic native tests and matched source replay for outward/inward
transitions, full-raster returns, caption protection, cached proof at zero
allowance, late cancellation, overlapping windows and dependency invalidation.
Exercise P010/P210/BGRA, dark/dense content, policy/viewport changes, restart,
shutdown and sustained overload. Cover 23.976/24/25/30/50/59.94/60 Hz, including
24-to-60 Hz conversion. Compare first-frame crop/caption correctness and useful
adoption alongside CPU/GPU/presentation tails, deadline misses, worker cost,
queue depth and memory on clean x64 Release builds and under live GPU pressure.
