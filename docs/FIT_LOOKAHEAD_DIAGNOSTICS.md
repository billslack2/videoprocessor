# FIT-owned lookahead diagnostics

The bb891e0c replay at 17:05:06 on 2026-09-22 showed the intended
composition result at sequence 3673 (`34..2126`), followed by accepted picture
geometry at 3687 (`68..2092`) and a true scope return at 3694 (`276..1884`).
The second resize remained after 14 frames. The ordinary preview rejected FIT
ownership, so there was no queued broad-picture certificate for that interval.

## Diagnostic scope

A confirmed FIT with matching source generation and exact stored picture base
may now collect the existing three-frame outward proof as a shadow diagnostic.
Moving-picture episodes, near-black episodes, and active translation drift are
excluded. All existing pixel, geometry, frame identity, queue membership,
continuity, and lookahead-depth proof checks still apply.

The diagnostic calls the same certificate builder as production but returns
only `passes`, `reason`, and `failedSample`. Its renderer branch never writes or
clears queued certificates, never enables inward proof, and never retires FIT.
Ordinary preview observations and timeline processing remain unchanged.
A passing diagnostic is **a certificate only**, not proof that model admission
or every eventual consumer check would permit publication. No threshold or
presentation behavior is changed.

## Normal log fields

`Alpha picture preview status` adds:

- `shadow_fit`: confirmed FIT was eligible for this diagnostic.
- `shadow_built`: queue membership/continuity allowed certificate evaluation.
- `shadow_pass`: the exact three-frame certificate passed.
- `shadow_reason`, `shadow_failed_sample`: rejection stage and zero-based sample
  index, or -1 when no individual sample failed.
- `shadow_first_pass`, `shadow_windows`, `shadow_passes`: retained diagnostic
  counters, distinct from production confirmation counters.

Rejected sampled windows use the existing `Alpha picture preview sample` lines,
including class, rejected axes, near-black evaluation, strip brightness and
texture. `inspection_ms` includes the additional sampling cost. Ordinary log
bundles remain coalesced to two per second with a two-second pending heartbeat.

`Alpha FIT lookahead shadow first pass` emits once per diagnostic context without
waiting for bundle throttling. It preserves the exact first/current sequence,
confirming sequence, trusted base, first and confirming candidate rectangles,
policy/continuity generations, and `runtime_apply=never certificate_only=1`.
`Alpha FIT lookahead shadow end` reports counters when eligibility ends.
Repeated source sequences do not count twice. Source/format/renderer/viewport,
base, policy, continuity, and sequence rollback reset diagnostic context.

## Replay and interpretation

Replay from established scope through the problem transition and continue
through the return to scope. Compare the first passing shadow sequence with
`Alpha source crop`'s actual accepted geometry sequence. An earlier passing
certificate identifies an opportunity to investigate the FIT owner veto; no
passing certificate means relaxing that veto alone cannot accelerate this
window. A recording is optional for this question. Capture the normal log;
verbose logging and configuration changes are unnecessary.

Additional work is bounded to at most three existing retention inspections per
eligible window. It introduces no new GPU readback or image copy. The runtime
cost must still be checked in the diagnostic replay.

## Verification

New independent tests in `FitLookaheadDiagnosticTests.cpp` use actual P010
pixel evidence and compare diagnostic results with the existing proof. They
cover weak upper-strip values from the replay, provisional samples, identity
and continuity failures, unchanged inputs, FIT publication vetoes, and retained
first-pass metrics. Existing regression tests remain unchanged.

Build and test evidence is stored under `artifacts/lookahead/fit-shadow-*`.
Live usefulness requires a fresh replay on this diagnostic build; earlier logs
cannot supply the queued strip measurements that were previously skipped.

Seven focused tests and the full native suite passed (**1,450/1,450**).
No existing test expectations were changed. Results: `fit-shadow-focused.trx`
and `fit-shadow-full.trx` under the artifact directory above.
