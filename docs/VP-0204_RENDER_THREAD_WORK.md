# VP-0204: bounded render-thread diagnostics and optional picture proof

This change addresses render-thread audit findings 2, 3, 4 and 5 together. It
starts from `v1.4.02-beta` at `fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`.
The explicit-Apply LUT work in PR #191 is separate.

## Behavior and ownership

* Logging producers format the existing printf message into a fixed 4096-byte
  record, reserve one of 8192 preallocated nodes, and publish it through Windows
  atomic sequenced lists. There is no producer queue mutex and no contention-drop
  path. A paused producer owns only its reserved node; completed publications can
  still drain. The writer detaches publications in FIFO order, copies up to 256
  records to private scratch, returns nodes before I/O, and writes a combined
  formatted byte buffer. Sparse traffic coalesces for up to 250 ms; routine flushes
  are limited to that interval, with a final shutdown flush. Queue plus both writer
  scratch buffers have an approximately 35 MiB fixed budget. Only actual pool
  exhaustion rejects a new open-session record; already accepted records remain
  owned until drained. Closing admission atomically retains outstanding producers
  so shutdown cannot finish ahead of an admitted publication. Startup/shutdown
  may wait for file I/O; ordinary producers never acquire an application mutex or
  wait for the writer. Windows atomic primitives are nonblocking, not a guarantee
  against OS scheduling delay or a strict per-thread latency bound. External
  plugin forwarding retains its callback contract. printf formatting remains on
  the caller because variadic pointer arguments cannot safely outlive the call.
* The NVIDIA BT.2020 worker constructs, uses and destroys its backend/controller
  on one thread. Driver calls never hold the mailbox/status mutex. Requests
  coalesce to the latest target and carry a generation; obsolete read completions
  cannot initiate a SET. A SET already in progress cannot be interrupted, so
  ownership remains dirty until the next current intent or retirement restores
  it. Cached status rejects obsolete completion publication. Restoration remains
  a synchronous lifecycle operation executed by the worker, including retryable
  failed restoration and terminal shutdown abandonment.
* Optional lookahead proof receives an explicit caller-owned budget. Expensive
  samplers check it inside bounded batches/rows and unwind with a dedicated
  cancellation exception. The renderer discards the incomplete prefix before
  publication. No timeout is represented as a black pixel or a valid proof.
  Mandatory live safety calls use no budget and still finish their current-frame
  scans. Cooperative checkpoints bound application work between checks, not OS
  scheduling delays or a driver call; measured deadline lateness remains visible.
* One output batch always caches independent raw/global/AR-darkness measurements,
  including a single scheduled output. Retention reuses these measurements but
  still inspects current excluded bands for each distinct crop base. It never
  reuses prior-frame crop decisions. Ready raw worker results may seed this cache
  only after full frame identity/domain, pixel layout/range/encoding and exact
  shared ownership match. The queued format-generation adapter is rebound to the
  live transport generation only after those checks. Caption-refined copies are
  never imported as independent native evidence. Policy/continuity/base changes
  retain the established invalidation rules.

## Diagnostic interpretation

`VP async log telemetry` identifies process and transport and reports admission,
processing, true pool-full rejection, queue/pool high-water, bounded storage bytes,
truncation, combined write count/bytes/failures/time and flush count/time. The legacy
`dropped_contended` field is always zero: publication no longer has a mutex.
Write-byte counters include formatted timestamps, messages, newlines and writer
reports; snapshots precede their own report write. These measure writer API calls
and bytes, not physical disk operations. Buffering reduces calls/flushes; preserving
more messages can increase total bytes versus the lossy prototype.

`NVIDIA BT.2020 worker` accompanies existing render-load summaries. Request age,
coalescing, obsolete completions, driver total/peak and active-call age can expose
long NVAPI calls while renderer-facing status remains independently available.

`VP source measurement interval` reports interval batches/outputs, API requests and
hits, actual global/raw/darkness/retention scans, exact worker imports/rejections,
measurement total/peak and subtitle scans. Counts cover ordinary single-output
playback too. Measurement time excludes fixture preparation and cached returns;
retention scan time excludes the reused independent scans.

`Alpha optional proof budget` reports cooperative cancellations; existing preview
elapsed/late counters and rate-limited abort stage identify which proof exhausted
its budget. Incomplete work is never a successful future proof.

## Evidence and limits

The audit observed a roughly 24-line/s session average and bursts around 315/s,
but old logs cannot establish producer-lock latency or NVAPI-induced stalls.
One 23.976-to-59.95 session showed 48/48 incomplete/late previews, averaging
2.758 ms (3.567 ms peak) against the unknown-phase 1 ms allowance. Its active
CPU work exceeded some display periods but did not exceed the 41.708 ms source
period; this is not evidence of dropped source frames. New counters are intended
for comparison on the same workload after review and an explicitly requested
deployment.

Validation results are recorded in the PR. Deterministic stall/cancellation tests
and synthetic warmed 4K comparisons establish ownership, semantic equivalence
and scan reduction. They do not establish a measured reduction in live jitter.
Source pixel capture and active configuration edits are outside this change.
The authorized local deployment preserves the installed startup/subtitle fixes in
a separate integration branch; PR #192 does not absorb those unrelated changes.


## Fullscreen activation follow-up

The earlier five-second activation change (PR #186) is already in beta and the
installed build. The installed log records an unsuccessful foreground request;
it does not establish why Windows rejected it. Code review also found that the
single attempt could be consumed before startup, a renderer replacement or a
fullscreen retarget had revealed the current window.

A GUI-owned session now retains the original five-second deadline and requires
the existing validated first-live-frame reveal for the current HWND and renderer
generation. If the deadline arrives first, intent remains pending without polling;
the successful reveal schedules the remaining delay, or one timer wake if overdue.
The timer also verifies the current rendering target and absence of a covering
transition. Stale messages, targets and generations cannot consume the attempt.

The native request is still attempted once per fullscreen entry, including when
Windows denies it. Renderer restarts and display placement cannot rearm it;
fullscreen exit and shutdown cancel it. Windowed rendering never requests focus.
No input simulation, focus retry loop or new setting is introduced. Diagnostics
record readiness deferrals and, on the one attempt, current generation, previous
foreground process, requested/acquired result, keyboard focus and deadline delay.
Ten deterministic session tests cover deadline/readiness order, stale messages,
host/generation changes, cancellation and one-shot denial behavior.


## Memory-queue revision after live contention loss

The first deployed prototype reported 459 contention drops alongside 3163 accepted
records over the observed monotonic writer stream, despite a high-water of only
15/1000 records. This was admission loss, not exhausted memory or measured disk
backpressure. The user's follow-up replaces that tradeoff rather than simply
increasing the old mutex queue. Historical logger datasets in the measurement
report describe the prototype; updated native/stress/latency and post-deployment
write-volume evidence are recorded in PR #192 and the authoritative VP-0204 story.

The implementation uses Windows' documented nonblocking sequenced list operations
with aligned headers/nodes, instead of a bespoke ABA-prone pointer stack. Ownership
moves free pool -> producer -> published list -> writer -> free pool. Detached
list reversal uses a separate writer-private link. Shutdown's closed bit and
in-flight producer count share one atomic, so a reserved record cannot arrive
behind a completed final drain. See [Windows interlocked singly linked lists](https://learn.microsoft.com/en-us/windows/win32/sync/interlocked-singly-linked-lists).

## Current-beta merge, 2026-10-25

PR #192 at `43b81e9063e0531f7ac2b1f589238d6ad6d37d33` is integrated
with beta `6ba412bdb2c05c6a6fb0ec2358bc84320d3521bc`. The integration
retains #193's prepared proof reuse, cancellation and current-context checks,
#196's subtitle preparation isolation, #198's display scheduler and active
Smooth Motion bypass, and #199's promoted renderer selection. Shared cancellation
helpers already present in beta are retained rather than replaced by the older
foreground fallback paths from this PR.

The remaining change adds logger/driver isolation, exact-source measurement
reuse (including single-output sources), and readiness-gated fullscreen focus.
New diagnostics are log records only. No OSD rows, measurement-window reset
policy, displayed drop/repeat counters or plugin API changes are introduced.
PR #197 is excluded. The measurements above are historical controlled evidence;
fresh build/test results and exact tested identity are recorded in VP-0204's
story evidence. This source merge does not deploy or restart the player.