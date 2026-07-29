# VP-0055: Display-rate outlier quarantine and transition warm-up

## Status

Review.

- Root cause confirmed: a short first wait could seed the estimator near half
  the physical period, after which ordinary waits were rounded to two
  intervals. Numerical stability then reinforced the false 2x result.
- Implemented a deterministic candidate-validation policy covering freshness,
  stability, raw cadence, interval range, target-path plausibility, and
  harmonic compensation. Invalid candidates fail closed before OSD and
  scene-aware consumers.
- The Windows target-path family now seeds interval compensation but never
  replaces measured fractional precision. Per-nominal configured overrides
  remain authoritative and retain their OSD marker.
- A material mismatch requests at most two fresh measurement generations per
  monitor/target contract, allowing a newer credible result to replace a
  poisoned cumulative estimate without an infinite reset loop.
- OSD and decision diagnostics explicitly report warming, quarantined, or
  unavailable state; logs include candidate/raw evidence, generation, age,
  previous accepted value, reason, recalculation attempt, and blocked
  consumers.
- Confirmed implementation base: `v1.1.014-beta`
- Implementation branch: `codex/vp-0055-display-rate-validation`
- Source commit: `f70e868cc9ccf6f18dfcf3fc6e1c2906eb3e335a`
- Draft PR: `https://github.com/billslack2/videoprocessor/pull/25`
- Validation: x64 Release solution build passed with Visual Studio 18.7
  MSBuild; the full native suite passed 205/205. Focused cases cover clean
  23.976 and 59.94/60, explained scheduler gaps, startup zero, unstable,
  stale, and non-finite candidates, the recorded 2x incident, and a real
  refresh change.
- Review focus: confirm tolerance/retry policy and exercise an Alpha <->
  DirectShow/display-mode transition on the affected machine. Proposed
  decision is merge after review and live transition validation.

## User story

As a VideoProcessor user, I need the measured display rate used for OSD,
timing, and scene-aware calculations to remain credible through renderer and
refresh transitions, so a transient or mathematically impossible measurement
cannot corrupt pacing decisions.

## Observed incident

The deployed log at `C:\Videoprocessor\vp\logs\vp_debug.log.2` recorded this
while the Windows target path was 23.976 Hz:

```text
21:41:41 DXGI WaitForVBlank=47.947828 Hz (fresh=1 stable=0
         compensated=567 raw=23.928594 Hz rawCount=283
         rawGap=23.374..81.932ms); Windows target path=23.976000 Hz;
         DXGI-target=+999826.0 ppm; selected=47.947828 Hz (measured DXGI)
```

Later samples continued near 47.95 Hz despite a raw cadence close to 23.976
Hz. The selected value is an apparent 2x/harmonic compensation error, not a
plausible display rate. Other transition logs also publish `selected=0.000000
Hz (measured DXGI)` before fresh/stable samples are available. DWM timing was
unavailable on this system (`0x80070057`), while the Windows target-path rate
remained a useful nominal reference.

The failure was observed around Alpha/external-renderer and display-mode
transitions, but user reports indicate display-rate issues can also occur in
other cases. This story must solve the measurement-selection contract, not
special-case one renderer.

## Required behavior

1. Treat the Windows target-path rate as a nominal cross-check, not as a
   replacement for a valid measured fractional rate. Keep the existing
   per-nominal user override contract intact.
2. Validate every candidate DXGI measurement before it becomes the selected
   measured rate or feeds PPM, delivery, scene-aware, or OSD calculations.
   Validation must consider sample freshness, stability, raw interval quality,
   plausibility against the nominal rate, and obvious harmonic/factor errors
   such as the observed 2x result.
3. Quarantine zero, non-finite, stale, unstable, wildly divergent, and
   harmonically inconsistent candidates. Do not expose a quarantined value as
   the active measured display rate.
4. Reset measurement state on monitor/display-mode/renderer transitions, then
   enter an explicit warm-up state. Until a candidate passes validation, use no
   measured display rate for dependent decisions; retain a prior value only if
   it is explicitly compatible with the new display contract and label it as
   retained rather than fresh.
5. Avoid a false acceptance merely because a bad compensated result becomes
   numerically stable. The raw cadence and compensation decision must be
   independently explainable.
6. Preserve valid high-precision DXGI measurements (for example 23.976,
   59.94, 60.000) rather than snapping them to rounded nominal values.

## Investigation and design work

- Trace the wait-for-vblank sample window, gap rejection, raw-rate estimator,
  compensation logic, stability criterion, and selected-rate publication.
  Identify exactly why `raw=23.928594` produced `47.947828` with
  `compensated=567`.
- Inventory every consumer of the selected display rate, including OSD,
  scene-aware correction, refresh selection, PPM/delivery calculations, and
  renderer-specific telemetry. Define safe behavior for the warm-up/
  unavailable state at each consumer.
- Define tolerance rules by nominal refresh family and a clearly logged
  harmonic-detection rule. The rule must tolerate real fractional differences
  and ordinary measurement noise, but reject an approximately 2:1 mismatch
  (about +1,000,000 ppm in the incident).
- Decide whether candidate selection requires a minimum count/time after each
  transition and whether the raw/compensated estimator needs an additional
  confidence measure. Avoid delays that degrade normal startup unnecessarily.
- Add focused deterministic tests for sample sequences: clean 23.976/59.94,
  startup zeros, jitter/gaps, stale samples, 2x compensation, and a real
  refresh change.

## Diagnostics

Log one rate-decision record on material changes and a bounded periodic record
while warming up. It must include:

- monitor and Windows target-path nominal rate;
- raw rate, compensated rate, sample count, interval/gap range, freshness, and
  stability;
- previous selected rate and its generation/age;
- decision (`accepted`, `warming`, `retained-compatible`, `quarantined`, or
  `unavailable`) with a precise reason; and
- every consumer that was prevented from using an unavailable/quarantined rate.

OSD must never present zero or a quarantined candidate as a normal measured
display rate. It should make an unavailable/warming state clear without
misrepresenting the Windows nominal rate as a measured result.

## Validation matrix

| Case | Required result |
| --- | --- |
| Clean 23.976 DXGI samples | Accurate measured fractional rate is accepted |
| Clean 59.94/60 samples | Accurate measured fractional rate is accepted |
| Alpha <-> DirectShow switch with refresh resync | Measurement restarts, warms up, then converges without stale/harmonic use |
| Incident-style raw ~23.93, selected candidate ~47.95 | Candidate is quarantined; no consumer uses it |
| Zero/no samples immediately after transition | Report warming/unavailable, not 0 Hz as an active measurement |
| DWM unavailable | Valid DXGI can still be accepted with Windows target cross-check |
| Real monitor refresh change | Incompatible old value is not retained; new valid measurement is accepted |
| Per-nominal configured override | Existing override semantics and OSD marker remain unchanged |

## Acceptance criteria

- A 47.95 Hz result for a 23.976 Hz target cannot become the active measured
  display rate or influence dependent timing decisions.
- The code identifies and logs the compensation/selection reason for every
  accepted or rejected candidate.
- Startup and transition warm-up do not publish 0 Hz as a normal measured
  result, and no dependent subsystem treats it as valid timing.
- Valid measured fractional rates retain their precision; user overrides and
  normal stable playback do not regress.

## References

- `C:\Videoprocessor\vp\logs\vp_debug.log.2` (2026-07-28 21:41:41 and later)
- `C:\Videoprocessor\vp\logs\vp_debug.log` (transition warm-up examples)
- Display timing source/selection code and OSD timing reporting in
  `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`
- Existing display-refresh override configuration and its tests
