# VP-0026: Alpha low-latency elastic queue

## Status

Done. Merged as [PR #7](https://github.com/billslack2/videoprocessor/pull/7)
into `v1.1.014-beta` at merge commit `b79355e` on 2026-07-27.

Completed implementation commits: `dfaf4aa` and `2c366e9`.

Alpha now has an independent remembered desired depth (safe default 4), a
configuration-only positive-integer `alpha_queue_size` override, an internal
bounded capacity, generation-safe one-time prefill, and a steady low-water
reserve. The UI and OSD truthfully present Alpha as `current / desired` while
DirectShow remains `R/C/T`.

Validation: full `Release|x64` solution build; all 56 automated tests passed;
user runtime validation confirmed the queue behavior is working well. The
telemetry-driven drift/correction work remains owned by VP-0024 and VP-0027.

Supersedes the queue-policy portion of VP-0008 and the corrective boundary
implied by VP-0017.

## User story

As an Alpha-renderer user, I want a small intentional elastic buffer that
absorbs startup stabilization and short timing disturbances while placing a
strict bound on added latency and exposing when sustained clock drift requires
a real frame correction.

## Design principles

- Desired depth and maximum capacity are different concepts.
- Healthy depth may float around the target; do not force it back continuously.
- Buffering can delay a sustained clock correction but cannot eliminate the
  mathematical need for an eventual drop or repeat.
- Reuse VP's existing queue-size control, but give it truthful
  renderer-specific semantics: DirectShow retains its existing capacity
  meaning; Alpha treats the displayed value as desired CPU queue depth.
- Do not add a second user-facing Alpha queue control. Alpha's hard safety
  ceiling is an internal value derived from the desired depth.
- Support an undocumented configuration-only `alpha_queue_size` override for
  controlled Alpha A/B testing. It overrides the shared `queue_size` only when
  Alpha is selected, so renderer shortcuts can switch between madVR's normal
  capacity and a fixed Alpha target without editing configuration mid-run.
- Prefer a time/latency target so 24p does not acquire the same frame count and
  disproportionate latency as 60p.
- Keep the policy display-agnostic: derive timing decisions from measured or
  configured frame period and explicit measurement validity, never from an
  Epson-only or other single-display constant. Epson testing is a reference
  validation dataset, not the product target.

## Required design and implementation

1. Reuse the existing queue-size UI and renderer API. For Alpha, interpret the
   selected value as desired queue depth rather than the maximum size of a
   nonexistent raw/converted pair. Keep DirectShow/madVR behavior unchanged.
2. Define renderer-specific persistence/migration so switching between madVR
   and Alpha does not turn madVR's normal value (currently commonly 32) into a
   32-frame Alpha target, or turn Alpha's small target into an unsafe madVR
   capacity. Prefer one context-sensitive existing control with remembered
   per-renderer values over two simultaneous controls. Legacy `queue_size`
   configuration must have a documented, non-silent interpretation.
3. Add `alpha_queue_size` as an undocumented positive-integer configuration
   key. When present, it is Alpha's desired queue depth and takes precedence
   over the existing shared `queue_size`; it has no effect on DirectShow or
   madVR. Omitted or zero/invalid values fall back to the normal Alpha queue
   selection path and must be logged. Do not expose it in normal UI, command
   help, or released configuration documentation while the A/B experiment is
   in progress.
4. Derive Alpha's hard safety ceiling from the desired depth plus bounded
   transient headroom. The ceiling is diagnostic/internal, not a second normal
   operating target. Candidate target values such as two frames at 59.94 and
   candidate ceilings such as 4-8 frames are hypotheses only until VP-0024
   supplies valid, display-agnostic timing evidence.
5. Show Alpha as one queue, for example `Queue: current / desired`, with queue
   age. Do not show `R/C/T`. Expanded diagnostics may include the internal hard
   limit and DXGI in-flight work under their correct labels.
6. Perform one generation-bound startup prefill to the desired target. Do not
   repeatedly re-enter buffering from ordinary low-depth observations.
7. Define a floating healthy band, low/high boundaries, maximum frame age, and
   hard capacity. Report excursions without resetting the renderer.
8. Use VP-0024's queue age, source/present debt, render/swap duration, and
   measurement validity to distinguish:
   - healthy phase variation;
   - temporary render/presentation stall;
   - persistent capture-faster drift;
   - persistent display-faster starvation;
   - invalid/disjoint measurement.
9. At this stage, emit a generation-safe correction request when a boundary is
   approached, but do not add an intentional repeat or scene-aware drop.
   Existing hard overflow remains a separately counted last-resort fallback.
10. If an exceptional stall leaves a stale backlog, define a bounded recovery
   request toward the low-latency band. Do not silently carry hundreds of
   milliseconds of old video merely because the hard queue is not full.
11. Never use manual sleep pacing or an automatic renderer reset to control
   depth.

## Verification

- Compare queue-disabled/current behavior with each candidate target at
  23.976/24 and 59.94/60. Use the Epson as a reference path and exercise
  additional display paths where available; do not tune solely to one display.
- Exercise Alpha renderer shortcuts with and without `alpha_queue_size` and
  prove that Alpha uses the override while madVR continues to use `queue_size`.
- Measure added latency, startup time, normal depth distribution, oldest-frame
  age, empty waits, high-water excursions, and hard overflow.
- Induce short GPU/presentation stalls and prove that configured headroom
  absorbs the intended duration without unbounded latency.
- Run long enough to observe or confidently predict both drift directions.
- Exercise renderer switches, mode changes, resets, source changes, display
  profiles, and minimized/restore.

## Acceptance criteria

- Alpha has an explicit low-latency target and independent hard capacity.
- The existing queue control displays Alpha's desired depth and remembers a
  safe Alpha value independently of the DirectShow/madVR capacity.
- `alpha_queue_size` is logged as the effective Alpha source when present and
  never changes DirectShow/madVR queue behavior.
- Alpha presents one truthful queue in the UI/OSD rather than `R/C/T`.
- Normal queue depth remains bounded near the target without continuous
  corrective churn.
- Added latency matches the configured time target within one frame.
- Low depth alone never resets or repeatedly re-primes the renderer.
- Sustained drift produces a truthful pending correction request for VP-0027;
  it is not hidden by queue growth.

## Dependencies and follow-ups

Phase 1 is independent of VP-0024. VP-0024 is required before enabling the
telemetry-dependent policy and correction/recovery requests described above.
Supplies the buffer state and correction requests consumed by VP-0027.
