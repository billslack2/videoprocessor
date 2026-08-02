# VP-0072: Repair or explicitly constrain DirectShow no-stop timestamp modes

## Status

Backlog (2026-08-02). Live madVR characterization after the VP-0066 stable
baseline found three failures isolated to the modes whose stop-time component
is `None`. The seven modes that provide both usable start and stop timing are
working and are regression-locked by this story.

## User story

As a developer comparing DirectShow timestamp strategies, I want each exposed
start/stop mode either to deliver a valid, bounded sample timeline to madVR or
to be clearly identified as diagnostic/unsupported, so experimental modes
cannot silently starve madVR, fill VP's queue, or trigger reset loops.

## Live evidence and complete mode matrix

The UI exposes exactly ten modes. This story covers all of them:

| Mode | Current live result | Required disposition |
| --- | --- | --- |
| Clock-Smart | Working | Regression lock; no behavior change |
| Clock-Smart2 | Working | Regression lock; no behavior change |
| Rational-Rational | Working | Regression lock; no behavior change |
| Clock-Rational | Working | Regression lock; no behavior change |
| Clock-Theo | Working | Regression lock; no behavior change |
| Clock-Clock | Working | Regression lock; no behavior change |
| Theo-Theo | Working | Regression lock; no behavior change |
| Clock-None | No picture; madVR queues remain at zero; VP R/C/T about 0/1/1/32 | Repair the missing valid stop-time contract or mark unsupported |
| Theo-None | VP R/C/T fills continuously and normalization/reset repeats | Repair bounded delivery or mark unsupported |
| None | Picture appears, VP R/C/T about 0/1/1/32, madVR queues remain near one, and PTS lead is unavailable | Define diagnostic semantics and prove them, or mark unsupported |

`None` may remain a test-only mode if samples without timestamps cannot
reliably prime/schedule madVR. That is an acceptable outcome, but the UI and
logs must say so; VP must not pretend it is a production timing strategy.

## Scope and constraints

- Start from beta merge `d6dbd8b`, whose tested VP-0066 source is tagged at
  `f4a443e` / `vp-0066-stable-baseline-20260802`.
- Limit changes to the `Clock-None`, `Theo-None`, and `None` timing adapters,
  their validation, and any UI/logging needed to express unsupported modes.
- Do not change shared Smart, Rational, theoretical, PPM, epoch, queue,
  presentation-lead, or post-retarget normalization behavior unless a failing
  regression test first proves a shared defect.
- Do not infer madVR occupancy from VP queue depth. Use supported madVR
  configuration/runtime metadata only as context.
- Do not add a queue, worker transition, frame copy, periodic reset, or new
  steady-state timing controller.

## Implementation approach

1. Capture deterministic adapter-level traces for all ten modes using the same
   capture times, graph clock, nominal rate, epoch/reset, and queue inputs.
2. For each `*‑None` mode, record the exact `IMediaSample` start/stop validity,
   monotonicity, duration, discontinuity, and delivery outcome that reaches the
   DirectShow renderer boundary.
3. Repair a failing mode only if its intended contract can be expressed as a
   small adapter-level rule. Otherwise remove it from normal selection or
   label it diagnostic/unsupported with an actionable log message.
4. Re-run the complete ten-mode matrix at 59.94 SDR, then smoke-test 23.976
   HDR for every mode retained as supported.

## Acceptance criteria

- Automated parameterized tests exercise all ten modes through reset and a
  material discontinuity; they assert monotonic valid timestamps where the
  mode promises timestamps, bounded VP queues, and no self-triggered reset
  loop.
- Golden tests prove byte-for-byte-equivalent timing decisions for the seven
  currently working modes under their existing inputs.
- Every supported `Clock-None`, `Theo-None`, or `None` mode displays a picture,
  primes madVR without sustained starvation, keeps the configured VP reserve
  bounded, and reports meaningful timing telemetry.
- Any mode that cannot meet that contract is unavailable or unmistakably
  labeled diagnostic/unsupported in the UI and log, with no production
  recommendation.
- 59.94 SDR live validation runs at least 7,000 frames per supported mode with
  no sustained madVR drops/repeats or VP queue drift. Retained non-Smart modes
  also pass a 23.976 HDR smoke test.
- Rational-Rational, Clock-Rational, Clock-Smart, Clock-Smart2, Clock-Theo,
  Clock-Clock, and Theo-Theo retain their VP-0066 queue depth, latency trend,
  reset behavior, and madVR fill behavior.
