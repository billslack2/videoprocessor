# VP-0178: Assess P010 CPU waits versus GPU readback

## Status

Backlog (2026-09-10). Bounded performance investigation; no GPU implementation
approved or adopted. User requested assessment of cinillo2's GPU results.

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
  frames. This predates July 31 and remains in the current implementation.
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

No deployment/source changes in this assessment. Separate from madVR color
exit regression VP-0179 and from completed LEGACY controls VP-0177.
Exact CPU cause and GPU pixel correctness remain unverified.

## Primary technical references

- https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query
- https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/copying-and-accessing-resource-data
- https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-switchtothread
