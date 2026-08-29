# VP-0165: Evaluate safe active-picture lookahead refinements

## Status

Backlog (2026-08-29). Split from VP-0164 after its exact inward-certificate
fix was accepted and closed. This story is evidence-gathering and bounded
policy evaluation; it does not reopen VP-0164 or authorize a larger queue,
additional presentation latency, scene-derived crop geometry, or interpolation
of active-picture pixels.

## User story

As a scope-screen operator, I want potential lookahead improvements evaluated
against captured live transitions and performance telemetry so useful smoothing
can be adopted without weakening the exact pixel-safety guarantees or causing
new aspect, subtitle, dark-scene, or scene-cut regressions.

## Scope

1. Capture representative 24, 50, and 60 fps live traces containing confirmed
   aspect changes, hard cuts, one-frame flashes, near-black title cards,
   subtitles, and NLS on/off transitions.
2. Measure VP Renderer preview-analysis p50/p95/p99 time and render deadlines
   with lookahead disabled and at the current profile depths. Preserve the
   VP-0124 performance gate and identify the actual per-frame cost of the
   bounded global near-black certificate.
3. At 50/60 fps, compare lookahead depths three, four, and five only when those
   frames already exist in the queue. Do not increase queue target, capacity,
   prefill, or latency to manufacture availability.
4. Evaluate preview scene correlation only as a rejection or diagnostic signal
   when an exact pixel certificate is absent. Scene detection must never create
   or positively authorize crop geometry, and a false scene boundary must not
   reintroduce the confirmed one-frame flash fixed by VP-0164.
5. Measure whether shader or NLS-layout prewarming can reduce unrelated cold
   transition cost without changing crop/NLS pixels or blocking presentation.

## Acceptance criteria

- Every trial records source rate, configured and available lookahead, queue
  target/capacity, decision association, proof result, scene event, preview
  duration, render deadline result, and visible outcome.
- No candidate may advance detector cadence, add a confirmation vote or
  publication, bypass exact current-frame validation, or survive a stale
  identity, policy generation, continuity boundary, or near-black veto.
- A proposed depth change must show a repeatable improvement at 50/60 fps with
  no queue growth and no preview-p99 or missed-deadline regression.
- A proposed scene signal must prove value on captured cuts and flashes while
  remaining veto/diagnostic-only; ambiguous evidence fails closed.
- Shader/NLS-layout prewarming, if useful, is proposed as an independent
  pixel-neutral change with cold/warm timing evidence and lifecycle tests.
- Queue growth, detector-cadence reduction, scene-derived geometry, and NLS
  interpolation/crossfade require separate stories and are not deliverables.

## Dependencies

- VP-0082: Buffered active-picture lookahead for Alpha.
- VP-0124: Safe outward active-picture lookahead and preview performance gate.
- VP-0140: Nonblocking VP Renderer shader preparation.
- VP-0164: Exact inward per-frame proof certificate and diagnostics.

## Next action

Collect synchronized live logs and frame-rate-specific traces before changing
any runtime policy. Use the existing VP-0164 association/proof diagnostics as
the baseline.
