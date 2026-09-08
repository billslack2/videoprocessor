# Render-load OSD implementation

The current beta cadence/OSD policy is combined with the corrected same-frame
timing series through 8c3429df. The old Expected D/R and comparison presentation
experiments are excluded. The OSD shows GPU frame average/maximum milliseconds,
one CPU/GPU-budget average/maximum load row, and the source frame budget.
Native bitmap and fallback windows share drawing and dynamic height calculation.

## Measurement and cost

One preallocated ring contains 16 D3D11 query sets, each with a frame-owned
disjoint query and up to six timestamp pairs. The headline sums bounded source
upload, changed overlay upload, and core pl_render_image stages. Stage ends
perform an asynchronous context Flush to submit complete timestamp pairs; this
retains the validated correction for driver batching and Present contamination.
It is not a wait for GPU completion, but adds real command-submission overhead.

Staged timing alternates with the legacy pass-sum diagnostic. Libplacebo's
internal queries are suspended on staged frames to avoid nested disjoint
queries. The core diagnostic is derived from the same staged sample. Legacy
pass->last values have no proven source-frame provenance and never feed the
headline. Coverage is normally near 50% and is recorded in logs.

Polling uses DONOTFLUSH after at least two subsequently issued samples, stops
when the oldest query is not ready, and skips new timing when the ring is full.
No polling loop waits or spins. Telemetry locks use try-lock and count discarded
operations. Submission matching uses binary lookup in the bounded window.

Accepted results must match generation, source sequence, and successful
submission serial. Reject failed submissions, duplicates, expired records,
disjoint/invalid queries, and mismatched identities. Queue-generation resets
also reject an old in-flight submission that completes after the reset.

## Windows and resets

GPU average and maximum cover accepted samples in the trailing ten seconds.
Budget percentages use each sample's validated source/render period. CPU uses
GetProcessTimes on the existing UI timer regardless of OSD visibility; average
weights elapsed intervals clipped to their overlap with the ten-second window.
No claim of hardware-engine occupancy is made.

Optional warmup follows OsdTimingPolicy, disabled by default. Ordinary queue/HDMI renderer resets preserve completed GPU samples while
rejecting unresolved old-generation results. CPU resets on input lock loss, material source changes, and
renderer/host generation changes. Pipeline shader/profile changes clear windows
and session peaks. Backlog recovery drops pending query evidence while retaining completed samples
and session peaks. D/R holds its established estimate during same-contract
measurement pauses without assigning evidence weight to the gap; restart or
contract changes clear it. GPU budget falls back to the known input format rate
while the measured drift tracker recovers, without changing cadence correction.

## Compatibility and validation

Plugin ABI 19 protects the expanded shared render-load structure. GUI and plugin
must be built and deployed together from x64 Release. The checked-in custom
libplacebo timing dependency must match the installed dependency; no application
configuration is required for this integration.

Regression coverage includes same-frame identity, duplicate rejection, repeated
sources, old in-flight generation results, pipeline/session reset semantics,
optional warmup, independent millisecond/percentage peaks, and time-weighted
CPU window clipping. The full core and Config suites and live hardware telemetry
must be checked for each deployed build. Unit tests cannot establish driver
overhead or visual quality on their own.
