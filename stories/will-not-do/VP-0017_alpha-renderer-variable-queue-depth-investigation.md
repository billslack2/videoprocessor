# VP-0017: Explain variable alpha-renderer queue depth

## Status

Will Not Do. Superseded on 2026-07-25 by VP-0024 and VP-0026. Investigation
established that Alpha owns one CPU FIFO, reports no converted queue, and does
not include its active render or DXGI in-flight work in `R/C/T`; VP-0024 makes
that pipeline observable and VP-0026 replaces accidental depth with an
explicit low-latency elastic-buffer contract.

The original report noted that, especially at 59.94 Hz, the
alpha renderer sometimes shows approximately 20 queued items and sometimes
0–1, while raw and transformed queue counts remain equal and visible
performance/latency does not obviously change.

## User story

As an alpha-renderer user, I want queue-depth reports to correspond to real
buffering and pacing behavior, so I can tell the difference between healthy
back-pressure, a reporting race, hidden buffering, and a queue that is failing
to fill without causing an unnecessary reset.

## Investigation questions

- Are the displayed raw/transformed counts sampled at different points or
  under different locks from the actual producer/consumer queues?
- Do the counts include only VP-owned frames, or also GPU upload, libplacebo
  internal frame state, swapchain buffering, and frames already submitted?
- Is the 0–1 state a valid steady state at the selected pacing policy, while
  20 is a burst/back-pressure state, or does it indicate a timing defect?
- Are raw and transformed counts equal because transformation is synchronous,
  because the counters alias, or because a hidden transition discards work?
- Could the OSD sample be racing with enqueue/dequeue/reset and observe a
  transient without any presentation impact?

## Scope

Alpha/libplacebo only. Do not change queue thresholds, reset behavior, or
timing policy until the measurements establish a defect. In particular, never
reset solely because a queue is 0 or 1; that can be a valid state and could
create a reset loop.

## Required evidence

Add diagnostic-only, rate-limited correlation for one run:

- capture/enqueue/dequeue/present sequence IDs and timestamps;
- raw, transformed, renderer-owned, submitted, and in-flight counts at the
  same sampling boundary;
- queue capacity, producer wait, consumer wait, render duration, present
  result, and refresh/pacing estimate;
- renderer generation/reset ID and reason;
- whether a frame was dropped, repeated, skipped, or merely already submitted;
- the OSD sample timestamp and lock/sequence used to read it.

Compare 23.976/24 and 59.94/60 Hz, startup/warm-up, steady state, screen
profile changes, shader changes, color/metadata transitions, and renderer
recovery. Compare the alpha path with the established renderer only where the
measurement definitions are equivalent.

## Decision and implementation boundary

After evidence, choose one of these outcomes:

1. **Reporting artifact:** unify the sampling boundary/labels and document the
   legitimate queue states; no pacing change.
2. **Expected pacing behavior:** retain the behavior, add OSD/log context, and
   define safe high/low-water observations without treating low depth alone as
   failure.
3. **Real starvation or over-buffering:** write a focused follow-up change
   with a reproduced trace, explicit invariant, and validation against drops,
   latency, and renderer fill behavior.

Any corrective action must be generation-aware, bounded, and idempotent. A
single low-depth observation is never sufficient to reset the renderer.

## Acceptance criteria

- A log trace explains at least one 59.94 Hz run with high depth and one with
  0–1 depth, including whether each was healthy.
- OSD labels distinguish VP queue depth from submitted/in-flight buffering.
- Queue counters are sampled consistently and no longer imply a failure from a
  valid low-water state.
- If a defect is found, the follow-up fix has a reproducible trace and proves
  no new drops, starvation, latency regression, or reset loop.
- If no defect is found, the documented queue/pacing model and diagnostics are
  sufficient for future reports.
