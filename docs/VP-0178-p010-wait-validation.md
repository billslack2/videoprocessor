# VP-0178: P010 blocking-wait validation

Both AVERAGE and LEGACY share a helper pool. Helpers formerly looped on
this_thread::yield while idle; the caller spun on completion with _mm_pause.
The updated pool uses per-helper mutexes and predicate-based condition
variables for work and completion. Shutdown wakes parked helpers. Partial
startup is cleaned up by the actual started-thread count; pool failure falls
back to the existing single-thread SIMD kernel without retrying each frame.
Configuration reload permits a fresh pool attempt. Pixel kernels are unchanged.

## Measurement method

Before: 04d3988c plus only the benchmark test. After: the blocking-wait change
on the same branch/base and machine. Both x64 Release. Ryzen 7 5700G, 16
logical processors. Test-host process CPU time (kernel plus user), normalized
to total-machine CPU percentage; this is not whole-application madVR usage.
Eight rotating 4K input/output buffers, deterministic packed input, 16 warmup
frames, 400 ms idle, two seconds at 24 fps and two seconds at 60 fps for each
policy/helper count. Sleep-until pacing and OS accounting have finite resolution.
One short run per case; results establish removal of idle spin, not a precise
long-term whole-VP CPU promise. Zero means no CPU time measured in that window.

## Results

| Chroma | Helpers | Frames/s | CPU before % | CPU after % | Mean conversion before ms | Mean after ms | p95 before ms | p95 after ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| AVERAGE | 1 | 0 | 6.04 | 0.00 | — | — | — | — |
| AVERAGE | 1 | 24 | 6.71 | 0.68 | 2.202 | 2.190 | 2.657 | 2.582 |
| AVERAGE | 1 | 60 | 6.64 | 0.98 | 2.153 | 2.179 | 2.512 | 2.587 |
| AVERAGE | 2 | 0 | 12.33 | 0.00 | — | — | — | — |
| AVERAGE | 2 | 24 | 12.52 | 0.49 | 2.016 | 2.027 | 2.382 | 2.294 |
| AVERAGE | 2 | 60 | 13.16 | 2.14 | 2.024 | 2.066 | 2.271 | 2.353 |
| LEGACY | 1 | 0 | 6.01 | 0.00 | — | — | — | — |
| LEGACY | 1 | 24 | 6.49 | 0.44 | 2.050 | 2.092 | 2.342 | 2.497 |
| LEGACY | 1 | 60 | 6.62 | 1.22 | 2.155 | 2.102 | 2.575 | 2.455 |
| LEGACY | 2 | 0 | 12.89 | 0.00 | — | — | — | — |
| LEGACY | 2 | 24 | 12.51 | 0.20 | 2.195 | 2.073 | 2.626 | 2.473 |
| LEGACY | 2 | 60 | 13.43 | 1.99 | 2.357 | 2.072 | 3.238 | 2.453 |

At the default one helper, 60 fps CPU dropped from 6.64% to 0.98% (AVERAGE)
and 6.62% to 1.22% (LEGACY). Mean conversion time remained approximately
2.1-2.2 ms. Idle consumption dropped from about one fully busy logical
processor per helper to zero measured process CPU in the idle windows.

## Correctness and lifecycle

The independent pixel oracle passed for both policies and all conversion
methods, SIMD tail lengths and 720p/1080p/UHD/DCI resolutions. A new repeated
wakeup/reload test converts 768 changing frames through one reused formatter per
cycle, checks every output against scalar, and exercises 1/2/8/1 helpers,
both chroma policies, parked-worker reload and destruction over three cycles.
Partial thread-creation failure cleanup was source-reviewed, not fault-injected.
The benchmark is opt-in via VP_P010_CPU_BENCHMARK=1 and should run alone.

Logs and TRX results remain under the implementation worktree:
- p010-wait-baseline.log and x64/Release/TestResults/p010-wait-baseline.trx
- p010-wait-blocking.log and x64/Release/TestResults/p010-wait-blocking.trx
- p010-blocking-release-build.log
- p010-blocking-native.log and x64/Release/TestResults/p010-blocking-native.trx

No new configuration option is needed. This applies automatically to threaded
AVERAGE and LEGACY. Scalar conversion has no helper wait loop to change.
The GPU path is not implemented; VP-0179 madVR color-state work remains separate.
