# VP-0204 measured caller latency and analysis cost

Measurements on this Windows workstation during existing 4K capture playback.
The deployed player was left running on `6e53a6e` (renderer API 28). No deployment,
display-driver changes, configuration edits or source-pixel captures were made.
The original PR source is `ead594028431e4c1c43b5886e4da437a8f066444`;
the follow-up in this PR adds three missing direct-sampler checkpoints and a
regression. Logger/NVIDIA code is identical across those two measurements.

## Logging and display-driver isolation

| Controlled caller case | Original locking path during injected stall | PR caller latency |
|---|---:|---:|
| Logger periodic/final flush holds its producer mutex | 104.5–108.8 ms across 10 calls | Periodic-flush transport p99 0.7 us; max 1.6 us |
| NVIDIA READ or SET holds the reporter mutex | First blocked calls approximately 109 ms | Request p99 approximately 0.1 us; Status p99 approximately 0.2 us |

These are intentionally injected 100 ms dependency stalls, not observations of
100 ms real disk/NVAPI stalls. The original logger's QueueMessage implementation
was extracted verbatim; only its class name/private visibility changed for the
harness. Its writes and batch flushes already ran outside the queue lock and
remained responsive in the control. The problem was periodic/final flush scope.
The NVIDIA control reconstructs the original reporter's driver-held mutex
around the original controller with a fake backend; no actual NVAPI is called.

All 20 new logger transport loops completed 1,000 calls before their stall gate
was released. All 28 NVIDIA blocked trials completed 1,000 API bundles before
release; stale-generation checks passed 28/28. Pooled NVIDIA READ and SET each
have 14,000 Request/Status observations. Worst observed Request was 12.7 us;
worst Status was 3.9 us. No outliers were removed.

The transport result is not a full Log() latency claim. Actual DebugLog::Log
with mixed telemetry formatting and a controlled forwarding callback measured
p50/p95/p99/max **1.7/2.6/3.0/131.7 us** across 20,000 calls. Timestamp/file work
and the host forwarding/queue pipeline are separate from that callback case.

Try-lock logging trades waiting for explicit losses. All 5,000 attempts with
the bookkeeping mutex deliberately held were rejected. Eight-producer extreme
saturation retained/consumed roughly 1.25% after rejection plus eviction; that
stress result must not be described as a production loss rate. Ordinary traffic
and drop/flush counters must be assessed from live telemetry after deployment.

## Exact ready-worker reuse

The fixture is 3840x2160 P010, P210 or native BGRA8, with five patterns (scope,
full raster, dark texture, subtitle-edge panel and expansion). Each path gets
10 warmups and 101 interleaved timed trials. Fixture allocation and independently
ready worker measurement are outside the caller timer. That models an already
completed worker job, not free pixel analysis. Worker CPU work still exists.

| Format / fixture | Recompute median | Ready-worker median | Reduction |
|---|---:|---:|---:|
| P010 / scope | 0.5478 ms | 0.4164 ms | 24.0% |
| P010 / dark-textured | 1.1507 ms | 0.4311 ms | 62.5% |
| P210 / scope | 0.5485 ms | 0.4071 ms | 25.8% |
| P210 / dark-textured | 1.1165 ms | 0.4329 ms | 61.2% |
| BGRA8 / scope | 0.7731 ms | 0.6187 ms | 20.0% |
| BGRA8 / dark-textured | 1.7649 ms | 0.6388 ms | 63.8% |

Fallback-only timing remains a smaller, mixed result. The larger reduction is
conditional on an exact ready worker result: native raw/global/AR scans become
0/0/0 on the caller, while the current crop-base retention scan remains 1.
Full identity, interpretation and backing-owner validation is included in the
ready-import timer. Baseline scan counters are marked NA because that control
is not instrumented with cache statistics; its timing is measured.

These are elapsed CPU-helper stage costs, not whole-frame/render/GPU times.
Thread-cycle observations are retained separately; QPC elapsed includes OS
scheduling. Reuse requires a completed, exactly matching worker result, so real
benefit also depends on live worker-import readiness and rejection counts.

## Optional proof budget

A shared real QPC deadline covers three current/future retention helpers, as in
an optional expansion window. Both controls use the same supplied immutable
measurements. The control checks time only between helpers; the cooperative
path also checks within samples. This isolates checkpoint policy, rather than
claiming to replay every instruction in the old renderer. Each fixture/budget
has four warmups and 101 alternating pairs. Both 1 ms and 2 ms results, elapsed
percentiles, cancellation and publication counts are included in the CSV.

| Format / dark-textured fixture | Between-helper p95 lateness | Cooperative p95 lateness | Cooperative worst observed |
|---|---:|---:|---:|
| P010, 1 ms budget | 0.5113 ms | 0.0529 ms | 0.1026 ms |
| P210, 1 ms budget | 0.5285 ms | 0.0449 ms | 0.1334 ms |
| BGRA8, 1 ms budget | 0.4233 ms | 0.0395 ms | 0.1040 ms |

The first run exposed a real missing-checkpoint gap: direct profile, disputed
strip and visible-extent samplers bypassed SampleContext checkpoints. The PR
now checks those reads too. A deterministic stop-at-check-40 regression cannot
return partial safety and a subsequent mandatory inspection still succeeds.
The earlier measurements are preserved locally in before-dense-checkpoint-fix.

Checkpoint checks and exception unwinding have a cost. The CSV includes proof
completion counts: near the 2 ms limit the cooperative BGRA8 dark fixture
completed 66/101 windows versus 86/101 for the between-helper control in this
run. Optional proof can therefore be skipped more often; current-frame safety
continues on the mandatory path. No cancelled prefix was published. The
observed worst cases above are not hard real-time bounds; scheduling can delay
a checkpoint or exception unwind.

## Passive real playback baseline

A frozen 120-second sample of the installed build, 7,199 submissions, showed
3840x2160 SDR at approximately 59.941 Hz, full raster, NLS off and temporal motion
off. Active CPU averaged **4.180 ms**, peaking at **6.903 ms**. Source/display and
preview budget overruns were zero; no backlog recoveries or renderer/capacity/
cadence drops were reported. Swap wait averaged 10.797 ms; it is pacing wait,
not active CPU. Exact movie/title/time is not observable from the capture log.

This workload was healthy before these changes. It does not establish a live
jitter improvement. A matched replay with a later explicitly requested binary
swap is needed for that; the installed source also differs from the PR's beta
base, so a simple installed-to-PR comparison would include other changes.

## Reproduction and validation evidence

Production and native regressions were rebuilt as x64 Release. All **1,023**
selected native tests passed after the checkpoint correction, plus the isolated
measurement DLL passed. Config UI was not changed; the prior 104/108 result
with all four failures reproduced on clean beta remains the UI evidence.

Final fixture timings were collected after compilation finished and before the
regression suite, while the already-running capture player continued. Optimized
x64 harnesses use the exact source headers/Release library. NVIDIA/logging use
MSVC 19.51; the analysis library/harness use the repository v142 Release toolset.
QPC frequency is 10 MHz. GetThreadTimes has coarse 15.625 ms accounting on this
machine and can report zero for short loops; unconverted thread-cycle counts
are retained and not converted into invented CPU milliseconds.

Committed aggregate data:

* [Analysis reuse](measurements/VP-0204-analysis-reuse.csv)
* [Analysis budget](measurements/VP-0204-analysis-budget.csv)
* [Logging cases](measurements/VP-0204-logger-summary.csv)
* [NVIDIA cases](measurements/VP-0204-nvidia-aggregate.csv)

Raw per-call data, harnesses, extracted baseline headers, scripts, build logs,
TRX and passive runtime excerpts remain locally under
`artifacts/vp-0204/measurements/` in the PR worktree. Notable files include
`analysis_measurements.cpp`, `analysis-hook.targets`, `analysis-raw.csv`,
`logger-measurements.ps1`, `logger-readme.md`, `logger-results.json`,
`nvidia-reproduce.ps1`, `nvidia-analysis.json`, `runtime-baseline-summary.json`,
`analysis-final-run.log` and `results/dense-fix-native.trx`.
