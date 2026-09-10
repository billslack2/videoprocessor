# VP-0178: Assess P010 CPU waits versus GPU readback

## Status

Review (2026-09-10). Blocking work/completion waits implemented and deployed
for both AVERAGE and LEGACY as commit 6ca021b9. All 1,134 native tests passed;
measured converter CPU reduction and unchanged pixel output documented below.
GPU implementation remains outside scope. Await user playback/CPU acceptance
and review of draft PR #85.

## Report and evidence

Source: user-supplied excerpt from https://www.avsforum.com/goto/post?id=64830993
and GPU formatter startup/per-frame diagnostics. Reporter uses madVR and says
the implementation is based on a July 31, 2026 repository version; exact source
SHA and converter patch are not supplied. Reported VP CPU: 55-60% to 25% at
60p, 18-20% at 24p. This is credible as a reported CPU reduction, not proof of
lower end-to-end latency or unchanged pixels.

GPU: RTX 5070, D3D11 feature level 0xB100, ring=3, 3840x2160,
v210 stride=10240 bytes, input=22118400 bytes, P010 output=24883200 bytes.
Frame 0 total=34.89 ms. Complete later totals for frames 1-13:
10.76, 11.10, 8.89, 7.84, 8.31, 7.25, 8.13, 9.98, 7.24, 7.30,
6.76, 6.72, 7.17 ms. Mean=8.2654 ms, range=6.72-11.10 ms.
Frame 14 is incomplete and excluded. These are early samples, not a sustained
steady-state distribution. Most time is charged to readback (5.28-9.20 ms
for frames 1-13). Dispatch=0.00-0.01 ms is not established GPU execution time.

## Assessment

- D3D11 dispatch/copy work is asynchronous. A CPU timer around Dispatch mostly
  measures submission; synchronous Map/readback can absorb earlier GPU work,
  queued work and copy completion. Obtain timestamp/disjoint queries and the
  actual timing boundaries before attributing all readback time to transfer.
- A three-slot resource ring does not prove overlapped readback or three frames
  of latency. Inspect slot ownership, fences/queries and Map ordering.
- Under the described upload/readback/madVR-upload route, nominal payload is
  22.1184+24.8832+24.8832=71.8848 MB/frame, or 4.313 GB/s at 60p. This is a
  data-flow estimate, not a measured PCIe bandwidth or CPU cost. CPU conversion
  followed by one P010 upload has 1.493 GB/s nominal GPU upload at 60p.
- The existing CPU formatter helpers spin with this_thread::yield between
  frames. This predates July 31 and was present in baseline 04d3988c.
  Replacing the formatter may remove idle spin overhead as well as arithmetic.
  Do not label the full 55-60% as AVX2 cost without per-thread profiling.
- Local current CPU benchmark examples are about 2-3 ms, but hardware/build/
  workload differ; they are not an apples-to-apples speed comparison.
- VP-0177 supplies LEGACY versus AVERAGE and one-helper defaults. The old
  row-selection algorithm and modern averaging algorithm must not be mixed in
  an equivalence/performance claim. One helper still excludes the caller.

## Investigation plan and decision gate

1. Obtain converter source/patch, exact source revision, CPU and GPU/driver,
   configured helpers, conversion method and chroma policy, and timing scope.
2. On the same machine/source/madVR settings, compare CPU AVERAGE and LEGACY
   with one/two helpers; a CPU version with blocking idle waits; and GPU with
   identical chroma semantics. Test 23.976/24 and 59.94/60 Hz after warmup.
3. Measure per-thread CPU running time and wait stacks, whole-process CPU,
   GPU compute/copy/3D load, conversion p50/p95/p99, queue depths, added frame
   latency, dropped/repeated frames and madVR render time under normal shaders.
   Include sustained heavy madVR GPU load, not just an idle GPU.
4. Compare GPU output byte-for-byte with independent 10-bit pixel oracles:
   alternating-row chroma, luma ramps, excursion codes, packing low bits,
   padded stride/tails, edge pixels, DCI and alignment cases. Visual inspection
   alone is insufficient for 'no quality loss'.
5. Decide whether CPU blocking waits solve the CPU concern first. Consider a
   GPU route only with demonstrated net benefit and bounded latency/fallback.
   A future GPU-to-renderer texture handoff needs a verified madVR ingress
   contract; do not assume zero-copy interoperability exists.

## Boundaries

Initial assessment made no deployment/source changes. The subsequently
approved CPU-wait implementation is recorded below. Separate from madVR color
exit regression VP-0179 and from completed LEGACY controls VP-0177.
Exact CPU cause and GPU pixel correctness remain unverified.

## Primary technical references

- https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query
- https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/copying-and-accessing-resource-data
- https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-switchtothread

## CPU-wait implementation readiness

Continue on codex/p010-legacy-chroma at 04d3988c, preserving the explicitly
requested local VP-0174 base 16a8102f. Clean worktree:
E:\codex\videoprocessor\p010-legacy-chroma.
Both chroma modes share one helper pool; helpers spin/yield while idle and
the caller spins on completion. Replace both with predicate-based condition
variable waits using a mutex per helper. Preserve work publication and
completion ordering, wake idle helpers for shutdown, and safely clean up
partially started pools. No sleep polling, GPU conversion or pixel changes.

Validation: baseline and changed Release measurements for both chroma modes,
1/2 helpers, idle and paced 24/60 fps; repeated frame correctness, wakeup and
pool reload/destruction coverage; existing pixel oracle and full native suite.
This is a scheduling change. Hardware playback/image acceptance remains user
validation after a successful x64 Release build and authorized deployment.

## Blocking-wait implementation and validation

- Commit: 6ca021b95e0862c335f6b3b218e7dc647514f97a, on
  codex/p010-legacy-chroma; retains local VP-0174 base 16a8102f.
- Per-helper work/completion condition variables use mutex-protected
  predicates. Helpers sleep between frames; caller parks for pending output.
  Pixel buffers are published/completed under that synchronization. Shutdown
  wakes sleeping helpers and joins actual started count. Partial pool startup
  cleans up and falls back to existing single-thread SIMD without per-frame
  retries; configuration reload permits a new attempt. No new config required.
- Independent pixel oracle passed unchanged. New repeated-frame test compares
  768 changing frames with scalar across 1/2/8/1 helpers, both policies,
  parked reload/destruction and three formatter lifetimes.
- Clean x64 Release solution rebuild and committed incremental build passed
  with zero errors. All 1,134 native tests passed. Partial thread-creation
  failure cleanup was source-reviewed, not fault-injected.
- Same-machine short converter-harness measurements, 4K60, 16 logical CPUs:
  AVERAGE/1 helper CPU 6.64% -> 0.98%, mean 2.153 -> 2.179 ms;
  LEGACY/1 helper CPU 6.62% -> 1.22%, mean 2.155 -> 2.102 ms.
  Idle: about 6% per helper -> zero measured process CPU in 400 ms windows.
  Two helpers: AVERAGE 13.16% -> 2.14%, LEGACY 13.43% -> 1.99% at 60 fps.
  These are short, process-time measurements of the converter test host, not
  whole-application madVR performance claims. Full 24/60 fps data and method:
  E:\codex\videoprocessor\p010-legacy-chroma\docs\VP-0178-p010-wait-validation.md
- Baseline/changed benchmark TRX and full suite results are under the source
  worktree x64/Release/TestResults. Draft PR:
  https://github.com/billslack2/videoprocessor/pull/85

## Deployment

Refreshed the authorized deployment at C:\Videoprocessor\vp with the matched
Release executable and renderer DLL from 6ca021b9. Verified both installed
SHA256 hashes against build artifacts and backup hashes against prior files.
Backup: C:\Videoprocessor\vp\backup-before-vp0178-20260910-095246
Exact hashes: deployment.json in that directory. Configuration hash unchanged.
No settings or editor files changed. Config editor left running; VP was closed.
Remaining validation: user whole-VP CPU/image/playback comparison. GPU adoption
and separate madVR Rec.709 issue VP-0179 are not implemented by this change.
