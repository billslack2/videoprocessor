# VP-0182: ADVANCED v210-to-P010 chroma

Enable through the configuration file and restart VideoProcessor:

```ini
[directshow.conversion]
conversion_method: SIMD
min_core_count: 1
max_core_count: 1
chroma_downsampling: ADVANCED
```

The policy is case-insensitive. Omitting it still selects AVERAGE. AVERAGE and
LEGACY kernels and defaults are unchanged. No UI control is added. Existing
manual values are preserved by unrelated configuration-editor saves. This option
selects a filter only; it does not enable the P010 conversion route.

## Filter contract

ADVANCED is Lanczos-3, with its support widened for 2:1 vertical decimation.
It reads 12 chroma rows around output center `2*y + 0.5`. For tap `t=0..11`,
the source row is `clamp(2*y+t-5, 0, height-1)` and its unnormalized weight is
`sinc((t-5.5)/2) * sinc((t-5.5)/6)`, where `sinc(x)=sin(pi*x)/(pi*x)`.
Normalize the weights to sum to one and round to Q14:

```
60, 247, -557, -1092, 2220, 7314, 7314, 2220, -1092, -557, 247, 60
```

These integers sum exactly to 16384. Accumulate signed 32-bit values, add 8192,
divide by 16384, saturate to 0..1023 and shift left six bits for P010 storage.
Negative lobes are retained until the final clamp; nominal limited-range
headroom and footroom are not clipped. Luma is unpacked exactly, with no filter.
Chroma is centered at the same vertical position as AVERAGE. Filtering is
frame-based, like AVERAGE, and is not a field-aware interlaced downsampler.

SIMD/AUTO use AVX2 when available, with the existing blocking helper pool at
720 lines and above. Smaller frames use the same AVX2 kernel without helpers.
STANDARD/OPTIMIZED and the no-AVX2 fallback use the scalar version of this filter.
The helper count excludes the caller and still defaults to one. Source halos
cross helper boundaries; only frame edges are clamped. No frame-sized scratch
allocation or per-frame heap allocation is introduced by this filter.

Lanczos is an established windowed-sinc resampling option; see the
[FFmpeg scaler documentation](https://ffmpeg.org/ffmpeg-scaler.html). This custom
implementation defines the precise phase, coefficients and border handling above;
it is not claimed to be bit-identical to FFmpeg. Longer filtering improves
stopband suppression compared with a two-row average, but may ring on strong
color edges and costs more CPU. It is not universally better for every source.

## VP Renderer applicability

`CreateAlphaFormatter` in `LibplaceboVideoRenderer.cpp` uses v210-to-P210 when
`AlphaUsesP210Ingress` is true: v210 plus conversion override NONE. This unpacking
retains every 4:2:2 chroma row. It is not P010 downsampling, and none of the three
chroma policies affects it. Forcing P010 creates CV210toP010VideoFrameFormatter,
whose constructor loads the same directshow.conversion settings despite the
historical section name. The formatter is called before plane upload. Ingress
status distinguishes `P210 (lossless v210 4:2:2)` from `P010 (forced)`.

Normal v210 ingress to VP Renderer therefore has no need to discard half the
vertical chroma resolution. No renderer selection or ingress behavior changed.

## Validation

Independent tests derive coefficients from sine functions, compare integer
output exactly, and compare quantized output with floating Lanczos within one
10-bit code. They cover constant chroma, a full-range step, an impulse,
alternating rows, ramps and spatial patterns; frame borders, all 12-pixel SIMD
tails, UHD/DCI, guarded unaligned output, unchanged input and 1/2/8 helpers.
Existing AVERAGE/LEGACY pixel oracles are unchanged. Lifecycle tests now include
ADVANCED transitions and parked worker reload/shutdown. Configuration tests cover
case-insensitive ADVANCED, omission/invalid fallback, and UI save preservation.

x64 Release solution build passed (zero errors; existing compiler/Qt packaging
warnings remain). All 1,146 native tests passed in 49.5 seconds. ADVANCED analytic
oracle passed; lifecycle coverage now runs 1,152 changing frames across all
three policies. The focused config-editor preservation test passed for LEGACY
and ADVANCED. Source diff adds only the new dispatch and kernel to the formatter;
no previous kernel lines are removed or changed.

### Paced converter benchmark, September 10, 2026

Same Ryzen 7 5700G with 16 logical processors, x64 Release. Eight rotating 4K
input/output buffers, 16 warmup frames, 400 ms idle, then two seconds per rate.
CPU is test-host kernel+user process time divided by wall time and 16 processors,
not whole VP/madVR CPU. One short run per case; scheduler and CPU-accounting
resolution affect these readings. This is not a long-running playback test.

| Policy | Helpers | FPS | CPU % | Mean ms | p95 ms | Max ms |
|---|---:|---:|---:|---:|---:|---:|
| AVERAGE | 1 | 24 | 1.02 | 2.117 | 2.666 | 3.033 |
| AVERAGE | 1 | 60 | 1.07 | 2.115 | 2.494 | 2.789 |
| AVERAGE | 2 | 24 | 0.29 | 1.945 | 2.271 | 2.404 |
| AVERAGE | 2 | 60 | 0.92 | 1.948 | 2.197 | 2.571 |
| LEGACY | 1 | 24 | 0.15 | 2.042 | 2.535 | 2.821 |
| LEGACY | 1 | 60 | 1.07 | 2.037 | 2.395 | 2.950 |
| LEGACY | 2 | 24 | 0.58 | 1.925 | 2.139 | 2.422 |
| LEGACY | 2 | 60 | 1.65 | 1.940 | 2.201 | 2.704 |
| ADVANCED | 1 | 24 | 0.97 | 3.590 | 5.659 | 6.690 |
| ADVANCED | 1 | 60 | 3.71 | 3.732 | 4.807 | 6.896 |
| ADVANCED | 2 | 24 | 0.92 | 2.704 | 3.791 | 3.971 |
| ADVANCED | 2 | 60 | 3.40 | 2.897 | 4.167 | 4.578 |

All six idle windows recorded zero process CPU time. Conversion fits within a
16.67 ms frame period in this sample, but this excludes the rest of the renderer
pipeline and does not establish end-to-end deadline guarantees. ADVANCED's
additional active CPU work is expected; it does not reintroduce idle spinning.

Local artifacts: advanced-release-build.log, advanced-native.log,
advanced-cpu.log and x64/Release/TestResults/advanced-{native,cpu}.trx.
No deployment or visual hardware acceptance has been performed for this feature.
