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

The clean x64 Release suite passes 634/634 tests. A sustained performance test
now compares the former 30-frame hot-zero-buffer workload with three runs of
120 measured frames rotating across four patterned 4K buffers. The regression
gate uses the median-of-three average and p95; both must remain below the
16.67 ms 60 fps frame period. Median sustained averages on the development
system were 2.2 ms for v210-to-P010, 4.6 ms for r210-to-RGB48, 5.6 ms for
R12B-to-RGB48, 11.0-12.7 ms for limited packed RGB-to-P010, and 9.8-11.1 ms
for R12-to-P010. The patterned rotating workload exposed a 20-39% cost hidden
by the hot-buffer test for R10b/R10l, while all median sustained p95 values
remained below 14.4 ms.
