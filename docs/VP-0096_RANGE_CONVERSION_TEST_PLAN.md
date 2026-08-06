# VP-0096 DeckLink range-conversion test plan

## Finding

The reported R12B concern is not a defect in the R12B-to-RGB48 converter.
DeckLink defines R12B and R12L as full-range RGB, 0-4095, so 12-to-16-bit
expansion must scale the full code space. Bit replication preserves both
endpoints and is substantially closer to normalized scaling than padding with
four zero bits.

The same review found related defects in the 10-bit RGB paths. The SDK's
`DeckLinkAPIModes.idl` defines r210 as SMPTE-range RGB 64-960 and R10b/R10l as
SMPTE-range RGB 64-940. VP currently treats these formats as full range in
several conversion and renderer paths.

## Sources and authority

1. The bundled SDK declaration `3rdparty/blackmagic_decklink/DeckLinkAPIModes.idl`
   is authoritative for the nominal code ranges.
2. DeckLink SDK Manual section 3.4 is authoritative for byte/word packing and
   explicitly confirms the R12B/R12L full-range contract.
3. `docs/bmd_pixel_formats.pdf` supplements the packing descriptions for UYVY,
   HDYC, v210, and r210.

## Input assumptions by format

| Format | DeckLink contract or current VP assumption | Test focus |
|---|---|---|
| ARGB | `[A,R,G,B]`; nominal range is not stated. Full range is a VP assumption. | Literal channel order, alpha independence, stride, and separately named range-assumption tests. |
| BGRA | `[B,G,R,A]`; nominal range is not stated. Full range is a VP assumption. | Same coverage as ARGB, including Alpha masks and DirectShow conversion. |
| UYVY/2vuy | 8-bit 4:2:2, `[Cb,Y0,Cr,Y1]`, CCIR 601 semantics. | Codes 0/16/235/255 and 16/128/240, stride, P210 code preservation, P010 chroma siting. |
| HDYC | UYVY byte layout with Rec.709 semantics; not a distinct `BMDPixelFormat`. | Byte-identical unpacking with different color-system metadata. |
| v210 | 10-bit 4:2:2 packed into four little-endian words per six pixels; aligned rows. | Literal words, ignored pad bits, active tails, excursion codes, scalar/optimized/SIMD equivalence, P010 chroma siting. |
| r210 | Big-endian 2:10:10:10 RGB, SMPTE range 64-960. | Nominal endpoints, excursions, range-aware P010 conversion, RGB48 code preservation and signaling. |
| R10b | Big-endian packed RGB, SMPTE range 64-940. | Nominal primaries, endian/pad bits, limited conversion and signaling. |
| R10l | Little-endian equivalent, SMPTE range 64-940. | Same oracle as R10b and identical decoded output. |
| R12B | SMPTE 268M C4, big-endian words, full range 0-4095. | Literal nine-word block, 24 distinct components, full-range RGB48 expansion and normalized P010 downconversion. |
| R12L | C4 logical little-endian stream, full range 0-4095. | Independent literal vectors and decoded equivalence with R12B. |

## Renderer ingress matrix

These policies are implemented in different code and require independent
coverage; a converter-only test cannot validate renderer selection or metadata.

| Renderer | Automatic path | Forced P010 path / concern |
|---|---|---|
| Alpha/libplacebo | ARGB, BGRA, r210, R10b, and R10l upload as native RGB. R12B/L convert to P010. UYVY, HDYC, and v210 convert to P210. | All inputs use conversion helpers. Native 10-bit RGB is currently labeled full range; r210's 960 endpoint may not fit a generic limited-RGB model. |
| madVR / GenericHDR DirectShow | ARGB/BGRA, R10b/l, and R12B/L convert to P010; r210 converts to RGB48; UYVY/HDYC/v210 normally pass through. | All formats convert to P010. Nominal-range metadata currently follows only a user override and can remain unknown despite an explicit formatter contract. |
| MPC DirectShow | ARGB/BGRA, R10b/l, and R12L use P010; r210/R12B use RGB48; v210 uses P210; UYVY/HDYC pass through. | HDYC, r210, and R12B are not selected correctly by the current forced-P010 branch. |
| Generic DirectShow / EVR | R10b/l and R12L use P010; most other inputs pass through. | The override is honored only for v210. Direct R12B uses an incorrect 4-bit media-type bit count. |

The next policy seam should be a pure `IngressPlan` mapping tested across every
format, renderer, and automatic/forced mode. Each result should specify the
formatter, media subtype, nominal range, meaningful depth, bit shift, and
whether 4:2:2 chroma is preserved.

## Initial tests and expected state

### Passing documentation guards

- R12B-to-RGB48 preserves full-range mappings: 0x000 to 0x0000, 0x100 to
  0x1001, 0xEB0 to 0xEB0E, and 0xFFF to 0xFFFF.
- The literal R12B ascending-byte golden block detects packing and channel
  permutation across all nine words.
- Existing UYVY/v210 P210 tests preserve 4:2:2 codes without range expansion.
- Renderer helper tables enumerate every current DeckLink capture encoding.

### Intentionally failing characterization tests

- r210-to-RGB48 must preserve limited codes by zero-padding: 64 to 0x1000,
  960 to 0xF000, and excursion 1023 to 0xFFC0. Current bit replication gives
  0x1004, 0xF03C, and 0xFFFF.
- R10b/R10l limited BT.709 color bars must produce P010 red `(250,409,960)`,
  green `(691,167,105)`, and blue `(127,960,471)`, with black/white at 64/940.
- r210 uses the same output oracle only after normalizing its distinct 64-960
  RGB input span.
- R12-to-P010 must use `round(code * 1023 / 4095)`. The current shift-and-clamp
  implementation disagrees for 2,046 of 4,096 source codes.
- Direct R12B media types must report 36 bits per pixel, not `36 / 8 == 4`.
- UYVY and v210 P010 converters must follow one deliberate vertical chroma
  policy. UYVY currently rounds the two-row average while all v210 paths select
  chroma from the even row.
- Formatter output contracts must be input-dependent: limited for r210/R10b/l
  and full for R12B/L.

## Independent BT.709 oracle

For R10b/R10l input RGB 64-940, and for r210 after normalizing RGB 64-960:

| Color | Y | Cb | Cr |
|---|---:|---:|---:|
| Black | 64 | 512 | 512 |
| White | 940 | 512 | 512 |
| Red | 250 | 409 | 960 |
| Green | 691 | 167 | 105 |
| Blue | 127 | 960 | 471 |

All integer expectations use round-to-nearest and are independent of VP's
coefficient tables.

## Decisions deliberately left open

- Confirm DeckLink ARGB/BGRA nominal range with hardware/vendor evidence before
  changing the current VP full-range assumption.
- Define whether super-black/super-white excursions are preserved or clipped
  in each conversion path.

## Implemented result

- R12B/R12L-to-P010 uses exact normalized round-to-nearest through an immutable
  4,096-entry lookup table. This preserves correctness while avoiding repeated
  division in the 4K hot path.
- r210/R10b/R10l use limited-range BT.709 or BT.2020 conversion with their
  documented, distinct input intervals. Output P010 is explicitly limited.
- r210-to-RGB48 preserves all 10-bit codes in the high bits (`code << 6`) and
  declares limited 10-bit data with a six-bit container shift.
- UYVY and every v210 P010 implementation use the same rounded two-row vertical
  chroma average. P210 paths continue preserving every 4:2:2 chroma row.
- Alpha keeps R10b/R10l native with limited-range signaling. r210 uses the
  normalized P010 path because generic limited RGB cannot describe 64-960.
- madVR and MPC derive automatic nominal-range signaling from the formatter
  output contract. Generic and MPC forced-P010 selection covers all supported
  capture encodings.
- Native sparse RGB analysis uses the same limited-range reference conversion
  as displayed R10/r210 content and normalized R12 downconversion.

The clean x64 Release suite passes 648/648 tests. A sustained performance test
now compares the former 30-frame hot-zero-buffer workload with three runs of
120 measured frames rotating across four patterned 4K buffers. The regression
gate uses the median-of-three average and p95; both must remain below the
16.67 ms 60 fps frame period. The benchmark now covers every CPU formatter
route used by the renderer selection matrix, including P010, P210, RGB48, and
the no-op delivery copy.

| Sustained rotating 4K route | Before (average) | After average | After p95 |
|---|---:|---:|---:|
| v210 to P210 | 8.9 ms | 3.30 ms | 3.89 ms |
| UYVY/HDYC to P210 | 8.0-8.4 ms | 2.66-2.78 ms | 3.07-3.26 ms |
| ARGB/BGRA to P010 | 38.2-38.3 ms | 2.73-2.78 ms | 3.06-3.07 ms |
| r210/R10b/R10l to P010 | 11.0-12.7 ms | 2.63-2.77 ms | 2.92-3.22 ms |
| R12B/R12L to P010 | 9.9-10.4 ms | 4.57/4.28 ms | 5.41/4.80 ms |

The final direct RGB48 optimization follows the documented packing units rather
than assuming a specific video mode. R12B processes two 36-byte C4 blocks at a
time: 16 pixels, 48 components, 72 bytes, or nine 64-bit words. An eight-pixel
scalar tail keeps every width divisible by the format's native eight-pixel
packing valid. r210 processes eight pixels per AVX2 load and retains a scalar
pixel tail. AUTO selection checks both CPU and OS AVX state; scalar remains the
portable fallback.

| Sustained rotating 4K direct route | Scalar baseline average/p95 | Final AVX2 average/p95 | Average improvement | p95 improvement |
|---|---:|---:|---:|---:|
| r210 to RGB48 | 4.81 / 5.77 ms | 3.70 / 4.01 ms | 23.1% | 30.4% |
| R12B to RGB48 | 6.55 / 7.95 ms | 4.13 / 4.68 ms | 37.0% | 41.2% |

The final isolated median-of-three rotating-buffer checkpoint for all CPU
routes was:

| Route | Average | p95 |
|---|---:|---:|
| v210 to P010 / P210 | 2.23 / 3.05 ms | 2.46 / 3.48 ms |
| UYVY to P010 / P210 | 2.39 / 2.81 ms | 2.66 / 3.23 ms |
| HDYC to P010 / P210 | 2.35 / 2.67 ms | 2.62 / 3.13 ms |
| ARGB / BGRA to P010 | 2.67 / 2.68 ms | 3.00 / 3.01 ms |
| r210 / R10b / R10l to P010 | 2.50 / 2.46 / 2.53 ms | 2.72 / 2.72 / 2.81 ms |
| R12B / R12L to P010 | 4.66 / 4.06 ms | 5.38 / 4.65 ms |
| r210 / R12B to RGB48 | 3.70 / 4.13 ms | 4.01 / 4.68 ms |
| v210 no-op delivery copy | 1.40 ms | 1.61 ms |

When the benchmark ran inside the complete 648-test suite, the most conservative
RGB48 p95 was 4.79 ms and every other conversion p95 remained below 5.39 ms.
This loaded-system run still leaves more than 2x headroom against the 16.67 ms
4K60 frame period.

The optimized implementations remain bit-exact with their scalar oracles for
BT.709 and BT.2020, nominal endpoints, excursions, randomized inputs, scalar
tails, and threaded segment boundaries. ARGB/BGRA uses reusable thread-pool
work items at 1080p and above; no threads are created in the per-frame hot
path. R12 normalizes each complete eight-pixel C4 block once and retains the
exact 4,096-entry 12-to-10 lookup. The direct RGB48 routes now use AVX2
unpacking and remain at two helper workers; adding workers would consume more
capture CPU for already-sub-frame, memory-bandwidth-sensitive paths.

### Flexible-resolution verification

The converter suite treats active resolution as data, not as a signal for
format-specific image modification. Resolution-matrix tests cover 640x360,
720x480, 1280x720, 1920x1080, and 3840x2160 across ARGB, BGRA, UYVY, HDYC,
v210, r210, R10b, R10l, R12B, and R12L routes. They validate first and last
active samples as well as successful P010, P210, and RGB48 output. Existing
tests separately cover v210 padded tails at a synthetic 100-pixel width and
2048x1080/4096x2160 DCI rasters.

The legacy v210-to-P010 1280x720 path was removed. It forced the first and last
two active luma columns to zero, forced their chroma neutral, and bypassed the
selected Standard/Optimized/SIMD implementation. 720p now uses the same
stride-checked, padded-tail conversion as every other even width. An
independent uniform-code oracle and full-output comparisons prove that AUTO,
Standard, Optimized, and SIMD agree at every resolution in the matrix. Alpha's
formatter diagnostics now inspect the complete 720p active width instead of
excluding the formerly concealed columns.

### Live DeckLink RGB packing selection

DeckLink format detection reports signal family and bit depth; the application
chooses the host-memory packing requested from the card. VP now exposes three
startup-only preferences under `[decklink]`:

| Setting | Values | AUTO/default |
|---|---|---|
| `rgb_8bit_packing` | `AUTO`, `ARGB`, `BGRA` | ARGB |
| `rgb_10bit_packing` | `AUTO`, `R210`, `R10B`, `R10L` | r210 |
| `rgb_12bit_packing` | `AUTO`, `R12B`, `R12L` | R12B |

The defaults retain existing behavior. An alternate is checked with DeckLink's
`IDeckLinkInput::DoesSupportVideoMode` for the active connection and display
mode before capture restarts. Unsupported alternatives fall back to the
canonical packing, and diagnostics record the configured, requested, and
effective formats. Selection and capability checking occur only on startup or
a DeckLink format-change callback; no work is added to the per-frame path.

Policy tests cover all exposed alternatives, unchanged defaults, invalid
cross-depth values, supported selection, and device-rejection fallback.
Existing formatter, renderer-ingress, range, resolution, and sustained 4K
tests now also serve as reachability guards for the newly selectable formats.

The post-exposure rotating-pattern checkpoint confirms that the configuration
and capability policy adds no per-frame conversion stage:

| Newly selectable route | 4K average | 4K p95 |
|---|---:|---:|
| BGRA to P010 | 2.73 ms | 3.14 ms |
| R10b to P010 | 2.60 ms | 2.99 ms |
| R10l to P010 | 2.58 ms | 2.89 ms |
| R12L to P010 | 3.96 ms | 4.63 ms |

### HDYC / Rec.709 classification

DeckLink exposes one `bmdFormat8BitYUV` byte packing for 8-bit 4:2:2, while
VP's DirectShow surface distinguishes UYVY and HDYC semantics. Translation now
uses the already-resolved DeckLink colorspace: Rec.709 becomes HDYC and receives
`MEDIASUBTYPE_HDYC`; Rec.601, BT.2020, and unknown matrices remain UYVY and
continue carrying their separate colorspace metadata. This changes only
format-state construction and media negotiation, not captured bytes or the
per-frame converter.

Tests prove the colorspace classification, media subtype, renderer-ingress
coverage, and identical UYVY/HDYC conversion behavior across the standard
resolution matrix. Dedicated pattern hardware is not required for these
software contracts. Physical validation remains useful to prove that a given
driver/device reports the expected colorspace and that downstream renderers
honor the negotiated subtype and extended color metadata.
