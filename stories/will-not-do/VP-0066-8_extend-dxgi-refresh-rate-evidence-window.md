# VP-0066-8: Extend the DXGI refresh-rate evidence window

## Status

Will Not Do (2026-08-02). Extending a DXGI-only evidence window is superseded
by the implemented madVR detected refresh-rate source and periodic runtime
sampling in stable baseline `f4a443e`. The retained DXGI estimator is
supporting telemetry rather than the renderer timing authority.

## Parent and dependencies

Former parent: [VP-0066](../review/VP-0066_rearchitect-live-video-output-pipeline.md).

Related child: [VP-0066-7](VP-0066-7_refresh-rate-invalidation-and-reacquisition.md).
The implementation must use the shared display-rate estimator and readiness
state, not create a second DXGI timing path.

## User story

As a viewer using renderer and refresh-rate transitions, I want VP to use a
longer and more representative recent DXGI observation window, so transient
wait-for-vblank behavior cannot make the selected display rate appear stable
before it is actually reliable.

## Problem

The current DXGI refresh-rate evidence window can declare a candidate stable
too quickly after startup, renderer changes, or an HDMI/display resynchronizing
event. The resulting rate may fluctuate or be materially wrong even though the
short window satisfies the existing freshness and stability checks. Extending
the evidence period should improve confidence without delaying the first image
or treating a missing measurement as a valid zero rate.

## Required behavior

- Define a longer recent DXGI evidence window appropriate for declaring a final
  measured rate ready. The window must contain enough independent vblank
  intervals to reject short-lived cadence artifacts.
- Keep initial image presentation independent from final-rate readiness.
- Continue to discard stale samples and outliers rather than allowing a longer
  window to average bad data into an apparently plausible result.
- Use only recent samples after the invalidation generation established by
  VP-0066-7; old samples must not dilute a real refresh-family change.
- Preserve high-precision fractional rates such as 23.976 and 59.94 rather
  than snapping to nominal values.
- Read the Windows configured display-path rate through `QueryDisplayConfig`
  for the active display path (for example `\\.\\DISPLAY1`) and use it as an
  expected-family guardrail. It is a reference value, not proof of physical
  panel timing.
- Reject or quarantine DXGI candidates that materially diverge from the
  QueryDisplayConfig target-path rate. The tolerance must be expressed and
  logged in ppm and selected from evidence; a candidate roughly 10-20 ppm (or
  a stricter validated threshold) outside the target should not silently be
  accepted merely because a short DXGI window appears stable.
- Avoid excessive reacquisition delay once a genuinely stable rate is proven.
- Ensure the longer window does not create repeated reset/re-prime loops or
  startup image blackout.

## Investigation and design

- Review the current DXGI interval sample count, elapsed-time window, gap
  rejection, harmonic protection, freshness, and stability rules.
- Compare several candidate windows using recorded traces from startup,
  renderer swaps, HDMI resyncs, 23.976, 59.94, 60 Hz, and real refresh-family
  changes.
- Define the relationship between the Windows target-path rate and DXGI
  measurement. For example, a run may report:

  ```text
  Windows target path: 59.951000 Hz
  DXGI WaitForVBlank:  59.950620 Hz
  Difference:          -0.000380 Hz (about -6.3 ppm)
  ```

  The DXGI value remains the physical-timing evidence; the Windows value
  checks that the candidate belongs to the expected refresh family and helps
  reject implausible or harmonic results. The implementation must not replace
  measured fractional precision with the Windows nominal value.
- Choose the window based on measured evidence quality, not only a fixed timer.
  Record why the selected duration and minimum sample count are sufficient.
- Evaluate whether the target-path comparison should be a hard acceptance
  gate, a quarantine threshold, or a two-stage guardrail. Cover fractional
  measurement noise, mode changes, target-path reporting precision, and cases
  where QueryDisplayConfig is unavailable or reports no active path.
- Decide whether the window should be a non-configurable correctness policy or
  a diagnostic-only setting. User configuration must not be required to obtain
  safe timing.
- Confirm that display-rate consumers use the readiness state and cannot
  consume a provisional candidate as final timing.

## Diagnostics and tests

Log the evidence generation, window start/end, elapsed duration, sample count,
accepted/rejected intervals, raw and compensated rates, stability state, and
the exact reason the candidate is or is not ready. Include the active
QueryDisplayConfig path, target-path rate, DXGI-to-target difference in Hz and
ppm, tolerance applied, and whether the target was used as a guardrail or was
unavailable.

Tests must cover short unstable observations, a stable rate that becomes ready
after the extended window, stale samples aging out, explained long gaps, the
observed approximately-2x error, fractional 23.976/59.94 rates, startup, and
an actual refresh-family change. Replay tests must show that the longer window
improves selection without materially extending image startup or re-prime
latency beyond the approved readiness policy.

## Acceptance criteria

- A short-lived DXGI candidate cannot become final merely because it is briefly
  numerically stable.
- The selected window and readiness threshold are documented and covered by
  deterministic tests.
- Initial video remains visible while the measurement is warming.
- Final-rate consumers receive only a current, validated, ready measurement or
  an explicit unavailable/warming state.
- Existing valid fractional refresh measurements remain accurate.
- A candidate materially outside the active QueryDisplayConfig target-path
  rate cannot become final solely because it is numerically stable over a short
  window. A close candidate such as 59.950620 Hz versus 59.951000 Hz remains
  eligible and retains its measured precision.
- Logs make it possible to compare the old and extended evidence windows on a
  real affected machine.

## Out of scope

Replacing DXGI `WaitForVBlank`, adding a new timing source, changing the
display-rate estimator’s mathematical model, using a blind delay as proof of
stability, or changing user refresh-rate overrides.
