# VP-0205: Preserve useful lookahead

Lookahead enables crop and caption decisions on their first eligible displayed
source frame. This design preserves configured future-frame depth while moving
more complete proof ahead of presentation and replacing the fixed 2 ms ceiling
with admission based on measured presentation headroom.

## Status and branch scope

Implementation on separate draft [PR #193](https://github.com/billslack2/videoprocessor/pull/193),
2026-10-23. The canonical
[VP-0205 story](https://github.com/billslack2/videoprocessor/blob/main/stories/review/VP-0205_preserve-useful-lookahead-with-prepared-proof-and-adaptive-budgets.md)
tracks acceptance and outstanding live qualification.

The branch starts from the discovered current/default beta `v1.4.02-beta` at
`f7c7a8204f7a2cace68b3b40774406680af6c0d8`. Required cooperative analysis-budget
and immutable raw-result APIs are selectively reused from separate, unmerged
[PR #192](https://github.com/billslack2/videoprocessor/pull/192) at
`6e7ce51103269a5fd27ac5adcaa40c3b12a3635a`. Logger transport, NVIDIA, fullscreen,
and current-frame measurement-cache changes from that PR are excluded.

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
this historical sweep predates the implementation below.

[Capture aggregates](https://github.com/billslack2/videoprocessor/blob/main/stories/assets/VP-0205/capture-summary.json),
[helper results](https://github.com/billslack2/videoprocessor/blob/main/stories/assets/VP-0205/helper-budget-summary.json)
and [recorded source and limitations](https://github.com/billslack2/videoprocessor/blob/main/stories/assets/VP-0205/measurement-provenance.json)
are preserved with the authoritative story.

## Implemented worker and publication design

Configured lookahead depth is unchanged. Preparation selects the union of the
configured preview and the first three distinct future sources needed by outward
proof. Raw, subtitle, prepared-component and outward jobs use separate bounded
workers. Queued jobs prefer nearer sources, and obsolete requests cannot publish
into replacement slots. Render-thread submission uses try-locks and never waits
for a worker. Shutdown joins workers after presentation/conversion retirement.

Each queued source retains immutable raw/global/darkness measurements. Prepared
pixel-component priming requires an established bar-crop base; full-raster inward
acquisition continues through raw evidence without unused retention scans. A bounded
cache (two entries per component kind) retains complete retention, relative
contrast and inward-caption results. Keys include the exact owned source and
storage interpretation, full identity/domain/generations, base bounds/axes and
caption nomination. Negative complete results are distinct from pending work.
Workers publish immutable results only into their captured slots; the render
thread owns cache metadata, model, timeline and authority publication.

The outward worker accepts a complete three-source request before raw or retention
results are ready. It reuses exact ready dependencies or computes and publishes
missing ones itself, without waiting for another worker or another render pass.
This preserves the first-source opportunity when raw data arrives after priming.
Separate raw/component workers can race a cold fallback; duplicate computation is
bounded and fallback/reuse counters expose its cost.

Foreground evaluation reuses completed components before considering new pixel
work. A later cooperative cancellation retains previously completed components.
At zero new-scan allowance, bounded metadata processing can consume prepared
results; a cache miss never enters a foreground pixel evaluator. An eligible
pending caption keeps that source refinable and stops the publishable prefix.
An independent accepted subtitle-AR proof avoids waiting on an unused alternate
caption result. Expansion certificates replace existing ones only from the head's
first three consecutive complete samples. Temporal observations remain idempotent.

Current native-pixel, owner, stable-reference and live-model admission still run
at consumption. A prepared result does not bypass these checks. Changing context
invalidates reuse; partial scans never become darkness, clearance or crop evidence.

## Implemented presentation admission

Qualified foreground allowance is the reachable display-boundary lead less
`max(2 ms, 1.25 * (CPU tail + GPU tail))`. CPU and GPU maxima use eight fixed
250 ms buckets (about two seconds). New-source CPU cost is tracked independently
of cheaper repeated outputs. CPU/GPU overlap can make this sum conservative.
There is no universal 2 or 4 ms qualified ceiling. Unknown/stale timing falls back
to at most 1 ms; repeated outputs, backlog and unstable presentation get no new
foreground ticket. The deadline is fixed and cannot roll into the next interval.

Only existing nonblocking D3D11 timing results feed the GPU estimate. Source and
viewport epoch fencing rejects old samples, and confidence requires an actual
issue age within four qualified display periods. A delayed result cannot appear
fresh merely because it just resolved. A stale GPU maximum can preserve reserve
but cannot qualify a later boundary. These queries measure selected rendering
stages, not end-to-end Present or scanout completion; admission remains a
conservative heuristic that requires live GPU-pressure qualification.

## Telemetry and validation

Fixed-size render-owned aggregates report selected/ready/required depth,
complete work retained after cancellation, component cache hits, stage costs,
stop reasons, publication, actual adoption/rejection and effective source lead.
Async reporting distinguishes ready/late, invalid, no-change and stale results,
plus raw/retention fallback and reuse. Formatting occurs on the existing two-second
reporting path. Retained counts are events, not unique-source totals; completion,
publication and actual adoption are reported separately.

Native tests cover exact key invalidation, owner/storage/domain changes, caption
nomination equivalence and rejection, cancellation, nearest-source scheduling,
queue bounds, GPU issue-age/epoch freshness, presentation cadence and fixed
deadlines. Whole outward-window tests exercise three distinct sources, cold
raw/retention dependencies, raw completion after priming, zero-foreground proof,
first-source identity, no-change/invalid outcomes and obsolete-slot isolation.

Runtime source `be23c69ffb41bf54d3e112f37692a607d593930b` passed the full x64
Release solution build, including standard and beta renderer variants. It follows
a successful clean serial solution Rebuild at `19767f74`. All 1,801 selected
native regressions passed (zero errors/skips/failures); the native DLL and analysis
library hashes are identical across those builds. Independent threading and
principal reviews found no remaining blocker. Initial incremental PDB corruption
was resolved by the clean Rebuild. Nonfatal conversion/shadow warnings remain.

## Controlled three-source results

The final clean `be23c69f` harness passed all 15 synthetic owned 4K fixtures:
P010/P210/BGRA8, with three separate allocations and source identities 100–102.
There are six valid expansion fixtures, three unchanged and six invalid/dark or
caption cases. Each path rotates 101 trials per fixture and checks full retention
fields, certificate decision and effective first-source identity. P010/P210 use
explicit synthetic planar adapters; BGRA uses the production preparation factory.

| Valid expansion delivery | Valid / attempted | Foreground p50 range | Foreground p95 range |
| --- | ---: | ---: | ---: |
| Three direct retention scans, 2 ms ticket | 606 / 606 | 0.6034–0.8653 ms | 0.7874–1.1064 ms |
| Ready cache + certificate, zero new scans | 606 / 606 | 0.0039–0.0043 ms | 0.0158–0.0197 ms |

The direct 3/4 ms paths also delivered 606/606 valid proofs. Across all outcomes,
direct completion was 1382/1515 at 2 ms, 1512/1515 at 3 ms and 1515/1515 at 4 ms;
ready-only completion was 1515/1515. Cancellations here occur in negative/no-change
cases, so this fixture set does **not** demonstrate a useful-proof completion
improvement from raising the cap. It demonstrates lower foreground cost with
identical completed decisions, including delivery with zero new-pixel allowance.

The work still costs CPU before delivery. A complete cold outward worker request
computes three raw and three retention inputs: valid-fixture median service costs
1.1404–1.1977 ms. Warm exact reuse costs 0.0086–0.0097 ms, with zero new scans.
Across all fixtures cold median service ranges 1.1404–4.2720 ms, maximum 5.1730 ms.
Worker service, internal snapshot wall time and observed-ready time are separate.
Observed-ready is dominated by roughly 15–16 ms Windows timer scheduling: the
harness requests `Sleep(1)`, which does not guarantee a 1 ms wakeup. Those polling
results cannot estimate production dispatch latency. Fixture/raw/darkness setup
is excluded equally from direct and cache delivery, and worker cost is reported
separately. There is no live renderer, timeline adoption or GPU timing in this run.

[Reproducible harness, raw trials and provenance](https://github.com/billslack2/videoprocessor/tree/main/stories/assets/VP-0205/proof-delivery)
and [native/Release validation](https://github.com/billslack2/videoprocessor/tree/main/stories/assets/VP-0205/validation)
are preserved with the story. Live capture A/B, actual displayed-frame adoption,
GPU pressure, and sustained playback queue/memory tails remain qualification work.
Synthetic CPU timing cannot establish those outcomes. No deployment or configuration
change is part of this PR.
