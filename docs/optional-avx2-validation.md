# Optional AVX2 validation

## Scope

Base: `billslack2/videoprocessor`, `v1.3.005-beta`,
`c84863eaa59e03fe218de824c34b7c0220c4fd87`.
Implementation branch: `codex/optional-avx2`.

The x64 Release baseline is AVX. AVX2 remains available for supported machines.
This targets AVX-capable processors such as the reported i7-3820; it does not
establish support for processors without AVX. The reported crash has not been
reproduced on the affected computer, so that computer still needs a trial.

## Changes

- `VideoProcessor-Lib.vcxproj` and `VideoProcessor-VPRenderer.vcxproj` use
  `AdvancedVectorExtensions` for the general Release build.
- Eight `video_frame_formatter/*.AVX2.cpp` translation units retain
  `AdvancedVectorExtensions2`, with whole-program optimization disabled and no
  precompiled header. The linker cannot inline these kernels into baseline code.
- `CpuFeatures.{h,cpp}` caches the CPU/OS decision once per process. It checks
  CPUID leaf availability, XSAVE, OSXSAVE, AVX, FMA, AVX2, BMI1/BMI2, and XCR0
  XMM/YMM state. XGETBV is only executed after checking its prerequisites.
  The FMA/BMI checks are conservative because MSVC may emit those instructions
  for an AVX2 target. See [Microsoft's compiler documentation](https://learn.microsoft.com/en-us/cpp/build/reference/arch-x64).
- ARGB/BGRA, packed DeckLink RGB, r210/R12B RGB48, UYVY P010/P210, and v210
  P010/P210 dispatch to the isolated kernels only when supported. An explicit
  SIMD/AVX2 preference also falls back safely. The r210 forced-AVX2 bypass is fixed.
- Row and segment loops stay inside the AVX2 objects. Worker counts, threading
  thresholds, format math, chroma policies, and output layouts are preserved.
- Shared formatter declarations use forward declarations instead of pulling in
  ATL through VideoState. `VideoFrameEncoding.h` no longer includes ATL.
  This prevents AVX2-compiled ATL startup initializers from running before CPU
  detection. Baseline users include VideoFrame/VideoState explicitly.
- New native tests cover capability masks and automatic/explicit conversion
  equivalence across all eight formatters, including padded strides, vector
  tails, and worker-sized frames. Existing chroma/threading tests are retained.
- `tools/test_avx2_boundary.py` checks compiler settings and built Release COFF
  objects. It rejects LTCG objects, startup/TLS sections, and shared external
  helper functions that could be linked into baseline callers.

No user configuration change is required. Keep `conversion_method=auto` for
normal use. `VP_DISABLE_AVX2=1` is a diagnostic environment override read once
per process; it disables kernels and never forces unsupported instructions on.

## Validation commands

Build `VideoProcessor.sln` with MSBuild, `/m /p:Configuration=Release /p:Platform=x64`.
Run `vstest.console.exe x64/Release/VideoProcessor-Test.dll /Platform:x64`.
Run `python tools/test_avx2_boundary.py -v` after the build.

For fallback testing, start a fresh VSTest process with `VP_DISABLE_AVX2=1`.
The functional fallback run excludes only these performance cases:

- `DeckLinkConvertersSustained4K60PerformanceComparison`
- `CV210toP010VideoFrameFormatter4K60PerformanceSmokeTest`
- `V210P010PacedCpuBenchmark`

The ordinary Release run includes those tests with their existing behavior
and thresholds. The paced CPU benchmark remains opt-in, as in the baseline.

## Results

| Check | Result |
|---|---|
| Original Release native suite | 1,884/1,885 initially; missing renderer DLL layout corrected and the remaining test passed on rerun |
| Final x64 Release solution build | Passed |
| Final normal native suite | 1,892/1,892 passed |
| Final forced-fallback native suite | 1,889/1,889 passed; only three performance cases excluded |
| Release source/object boundary checks | 5/5 passed |
| Paired sustained converter benchmarks | Five pairs; all 16 routes below the existing mean/P95 16.67 ms thresholds in every run |
| Other crop/color-analysis cost checks | Four existing tests passed in both builds, repeated twice |
| Configuration UI suite | 78/81 passed initially; see below |

The configuration suite is **not fully green**. The three initial failures were
`native owner preserves Qt input and popup association`,
`real configuration dropdown remains clickable`, and
`stable reveal endpoint survives hidden Qt window`. On isolated reruns of both
the saved baseline and updated build, the first and third failed identically,
while the dropdown case passed in both. These are baseline/environment focus
failures, not evidence of an AVX2 regression. No assertions were weakened.

The new native suite adds seven tests. Both dispatch modes report their selected
capability state in TRX output. Conversion equality tests cover automatic and
explicit AVX2 preferences. The object audit verifies all eight Release objects
have no startup sections and publish only the intended kernel entry points.


## Performance comparison

Machine: AMD Ryzen 7 5700G, 8 physical cores / 16 logical processors, Windows.
Build: x64 Release, MSVC v142 14.29.30133. Baseline binaries were saved before
editing. Baseline/current benchmarks ran sequentially, including a reversed
order pair, without concurrent builds or other test suites.

The existing sustained benchmark covers sixteen routes at 3840x2160. Each
measurement uses the median of three batches; the table below compares the
median of five separate runs for each binary using the 120-frame rotating
pattern workload. Lower times are better. P95 columns are medians of the
reported batch P95 values, not pooled percentiles.

| Route | Baseline mean (ms) | Optional AVX2 mean (ms) | Change | Baseline P95 (ms) | Optional AVX2 P95 (ms) |
|---|---:|---:|---:|---:|---:|
| v210-to-P010 | 2.600 | 2.370 | -8.8% | 3.117 | 2.820 |
| v210-to-P210 | 3.624 | 3.112 | -14.1% | 4.642 | 3.820 |
| YUV 8-bit (UYVY)-to-P010 | 2.752 | 2.445 | -11.2% | 3.723 | 2.975 |
| YUV 8-bit (UYVY)-to-P210 | 3.004 | 2.824 | -6.0% | 4.181 | 3.364 |
| YUV 8-bit (HDYC)-to-P010 | 2.706 | 2.535 | -6.3% | 3.569 | 2.958 |
| YUV 8-bit (HDYC)-to-P210 | 3.152 | 2.772 | -12.1% | 4.023 | 3.292 |
| ARGB 8-bit-to-P010 | 2.947 | 2.938 | -0.3% | 3.326 | 3.289 |
| BGRA 8-bit-to-P010 | 2.928 | 2.886 | -1.4% | 3.194 | 3.192 |
| v210-noop-copy | 1.532 | 1.618 | +5.6% | 1.925 | 2.036 |
| r210-to-RGB48 | 4.179 | 4.191 | +0.3% | 4.828 | 4.703 |
| R12B-to-RGB48 | 4.396 | 4.454 | +1.3% | 5.175 | 5.078 |
| RGB 10-bit (r210)-to-P010 | 2.716 | 2.695 | -0.8% | 3.134 | 2.920 |
| RGB Big-Endian 10-bit (R10b)-to-P010 | 2.658 | 2.738 | +3.0% | 2.957 | 3.028 |
| RGB Little-Endian 10-bit (R10l)-to-P010 | 2.663 | 2.694 | +1.2% | 2.868 | 3.014 |
| RGB Big-Endian 12-bit (R12B)-to-P010 | 4.987 | 4.978 | -0.2% | 5.467 | 5.295 |
| RGB Little-Endian 12-bit (R12L)-to-P010 | 4.438 | 4.182 | -5.8% | 4.812 | 4.620 |

The first two updated-build runs preceded comment/include cleanup; the final
three used the final rebuilt binaries. Conversion algorithms and compiler
settings were unchanged between those builds. Full native and fallback suites
were repeated on the final binaries.

Across five pairs, converter median mean-time changes ranged from -14.1% to
+3.0%. The unchanged memcpy control measured +5.6% (+0.086 ms). That control
ranged from 1.385–1.590 ms in the baseline and 1.389–1.713 ms in the updated
build, demonstrating substantial run-to-run variation. R10b measured +3.0%
(+0.080 ms), R10l +1.2% (+0.031 ms). These small increases are recorded as
possible overhead; zero performance regression is not established.

The additional checks retained their sampling limits and passed in both builds.
In the last pair, 4K color inspection was 0.1544 ms baseline versus 0.1554 ms
updated; qualified remembered-return median was 0.6490 versus 0.6456 ms. These
short measurements are supporting checks, not a full renderer benchmark.


These are conversion microbenchmarks on this machine, not a guarantee of
unchanged full-player CPU usage or frame rate on every system. In particular,
the scalar fallback on the i7-3820 may have a lower throughput. Existing bounded
crop/color-analysis benchmarks were also compared to check for effects of the
general compiler-target change.

## Trial on the affected computer

Use the final x64 Release executable and matching
`vprenderer/VideoProcessorVPRenderer.dll` together, retaining the normal runtime
and shader directory layout. No deployment or configuration replacement was
performed by this task. Use a separate trial directory with a copy of that
computer's configuration; preserve the installed version for comparison.

First launch with the ordinary environment and use the failing input/rendering
path. On that processor, the CPU detector should select fallback automatically.
Check startup, capture, picture output, conversion time, and dropped frames.
If a crash remains, collect the exception code, faulting module, and address.
Third-party renderer DLLs and drivers have not been qualified on that CPU by
these tests. There was no non-AVX2 hardware or instruction emulator available
for this validation; disabling dispatch on an AVX2 machine does not emulate
illegal instructions on an older CPU.

Local build/test logs, saved baseline, and binary hashes are in
`artifacts/optional-avx2/`. Final application outputs are under `x64/Release/`.
