# VP-0204: bounded render-thread diagnostics and optional picture proof

This change addresses render-thread audit findings 2, 3, 4 and 5 together. It
starts from `v1.4.02-beta` at `fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`.
The explicit-Apply LUT work in PR #191 is separate.

## Behavior and ownership

* Logging producers format the existing printf message into a fixed 4096-byte
  record and try to admit it to a bounded 1000-record queue. They never wait
  for the queue mutex. Contention drops the new record; a full queue discards
  the oldest queued record, matching prior overflow behavior. The writer drains
  at most 50 records per bookkeeping lock and performs all timestamp formatting,
  file writes, reports and flushes after releasing it. Shutdown closes admission
  before draining accepted records. Startup/shutdown may wait for file I/O;
  ordinary producers do not. External plugin forwarding preserves its callback
  contract. printf formatting remains on the caller because variadic pointer
  arguments cannot safely outlive the call.
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

`VP async log telemetry` writer summaries report admission/processing, full-queue drops,
producer contention drops, depth/high-water, truncation and flush count/total/peak.
These allow a stalled disk to be distinguished from high producer traffic.

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
