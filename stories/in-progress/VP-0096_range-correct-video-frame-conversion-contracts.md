# VP-0096: Establish range-correct video-frame conversion contracts

## Status

In Progress (2026-08-06). Test-first range and routing analysis is active on
`codex/vp-0096-range-conversion-tests` in
`C:\Users\bslac\vp\vp-0096-range-conversion-tests`, based on the current
GitHub default branch `v1.1.016-beta` at `b6e2892`. Documentation review,
route characterization, converter corrections, renderer signaling, and
performance verification are implemented on the feature branch.

DeckLink documentation and independent engineering review are complete for
the initial matrix. An x64 Release build succeeds. The focused formatter run
initial run had 47 passing tests and 7 intentionally failing characterization
tests; the implementation checkpoint below records their resolution.

Implementation checkpoint (2026-08-06): all seven characterization failures
are resolved on the feature branch, renderer routing/signaling and native
analysis are covered, a clean x64 Release build succeeds, and the complete
suite passes 634/634 tests. The story remains in progress for hardware-path
validation and final integration review.

## User story

As a VideoProcessor user capturing limited- or full-range RGB and YUV sources,
I want every format converter to preserve or transform sample codes according
to one explicit range contract, so bit-depth conversion cannot subtly raise
limited-range black/white values, crush legal excursions, or cause the renderer
to interpret the converted frame with a different range than the converter
actually produced.

## Trigger and confirmed code findings

Forum review of `CR12BtoRGB48VideoFrameFormatter` challenged this operation:

```cpp
// Bit replication maps both endpoints exactly: 0x000 -> 0x0000, 0xFFF -> 0xFFFF.
return static_cast<uint16_t>((value << 4) | (value >> 8));
```

The bundled DeckLink SDK declaration resolves the premise: R12B and R12L are
explicitly full range 0-4095. Bit replication is therefore directionally
correct full-range precision scaling, maps both endpoints exactly, and is much
more accurate than `value << 4`. The reported `0x1001/0xEB0E` results are not a
limited-range padding defect. Bit replication is still an approximation to
exact `round(value * 65535 / 4095)` for intermediate codes.

The analogous 10-bit paths contain the real range error. The same SDK
declaration defines r210 as SMPTE-range RGB 64-960 and R10b/R10l as SMPTE-range
RGB 64-940. VP currently treats those three formats as full range in multiple
converter and renderer paths.

Review of `b6e2892` found related assumptions that must be decided together:

| Path | Current behavior | Required review |
| --- | --- | --- |
| `CR12BtoRGB48VideoFrameFormatter` | Correctly performs full-range bit replication, but formerly exposed no output contract | Preserve full-range expansion and declare a full-range 16-bit RGB48 contract |
| `CR210toRGB48VideoFrameFormatter` | Replicates 10-bit high bits into the low six bits as though r210 were full range | Select explicit limited-code preservation or deliberate range expansion, then match RGB48 metadata to that operation |
| `CDeckLinkRGBToP010VideoFrameFormatter` | Treats R210/R10b/R10l/R12B/R12L as full-range RGB, rounds 12-bit components to 10 bits, performs a full-range RGB-to-YUV matrix, and declares full-range P010 output | Prove or correct the input-range assumption, 12-to-10 rounding, matrix/offset, clipping, legal-excursion, and output-signaling behavior for every packed RGB encoding |
| `CARGBtoP010VideoFrameFormatter` | Maps 8-bit RGB endpoints to 10-bit full range and declares full-range P010 | Verify that ARGB/BGRA ingress is contractually full range and keep it distinct from limited-range packed capture RGB |
| UYVY/v210 to P010/P210 formatters | Shift samples into the high bits and declare limited range | Use these as preservation controls; verify scalar/SIMD and special-width paths agree and do not clamp legal excursions |
| No-op and native Alpha ingress paths | May avoid a CPU conversion while still relying on range metadata | Verify that bypassing a formatter preserves the same input-range authority and renderer interpretation |
| Direct R12B media type | Reports `36 / 8`, which evaluates to 4 bits per pixel | Report the documented 36-bit packed format accurately |
| UYVY/v210 to P010 | UYVY averages chroma vertically; v210 selects the even row | Choose one explicit 4:2:2-to-4:2:0 siting/filter policy and test all implementations |

`VideoState` currently carries encoding, EOTF, and colorspace but no captured
sample-range field. `VideoFrameFormatterOutputContract` describes only
formatter output, and the two RGB48 formatters currently return the default
unknown contract. Therefore a one-line shift change is not sufficient unless
the input and downstream interpretation are first made unambiguous.

## Scope

1. Establish an authoritative range matrix for every ingress encoding and
   conversion route used by Alpha, MPC Video Renderer, and madVR:
   ARGB/BGRA, UYVY/HDYC, v210, R210, R10b, R10l, R12B, and R12L to native,
   RGB48, P010, or P210 output. Cite the applicable DeckLink/SMPTE format
   contract and the renderer/media-subtype contract. Where documentation does
   not settle actual capture behavior, retain measured code ramps, PLUGE, and
   nominal/excursion samples from supported hardware.
2. For each route, record one of three explicit operations:
   - **container alignment**, which shifts bits and preserves code values;
   - **full-range precision scaling**, which maps both full-range endpoints
     with a defined integer rounding rule; or
   - **range conversion**, which deliberately maps limited and full nominal
     ranges with defined clipping or legal-excursion behavior.

   Do not describe bit replication as padding and do not perform range
   conversion accidentally as part of bit-depth conversion.
3. Add the smallest explicit input-range authority needed by the matrix. It
   may be a proven invariant for a format or an input-range field propagated
   from capture/configuration, but it must not be guessed from frame pixels,
   EOTF, colorspace, resolution, or the user's renderer-output range setting.
   Unknown range must fail safely with a documented default and diagnostic.
4. Preserve `CR12BtoRGB48VideoFrameFormatter` full-range expansion and add its
   truthful output contract. Correct `CR210toRGB48VideoFrameFormatter` to
   implement the chosen limited-range policy. Ensure the negotiated RGB48
   media type and DirectShow nominal-range signaling agree with the bytes.
5. Correct any equivalent defect found in
   `CDeckLinkRGBToP010VideoFrameFormatter`, including R12B/R12L unpacking,
   12-to-10 conversion, RGB-to-YUV coefficients/offsets, clipping, and output
   contract. Keep ARGB/BGRA full-range behavior only if its ingress contract is
   independently proven.
6. Audit all remaining frame formatters and no-op/native paths for the same
   class of error. Review bit alignment, rounding, nominal endpoints, legal
   excursions, component order, byte order, row stride/padding, odd or DCI
   widths, vertical orientation, scalar/SIMD equivalence, and formatter
   selection. Fix in-scope conversion defects; record unrelated defects as
   separate stories rather than expanding this story into renderer redesign.
7. Propagate the resulting range contract through Alpha/libplacebo and
   DirectShow/madVR sample/media-type construction. A renderer override may
   request an intentional output transform, but it must not retroactively
   redefine the range of source or already-formatted bytes.
8. Add concise startup/state-change diagnostics showing input encoding and
   range authority, selected formatter, conversion operation, output encoding
   and range, precision/bit alignment, and renderer signaling. Do not add
   per-frame logging.

## Required tests and evidence

- Table-driven conversion tests cover every supported input/output route and
  include zero, full-code maximum, midpoint, nominal limited black/white,
  chroma center/minimum/maximum where applicable, below-black/above-white
  excursions, and adjacent values that expose rounding differences.
- Limited-range preservation tests prove exact mappings such as r210 10-to-16
  `code << 6` and existing 8/10-bit P010/P210 container alignment wherever
  preservation is the selected contract.
- Full-range scaling tests prove exact endpoints, monotonicity, bounded error,
  and the documented rounding rule across the entire 8-, 10-, or 12-bit input
  domain. Endpoint-only black/white tests are insufficient.
- Packed-format golden tests independently verify R210, R10b, R10l, R12B, and
  R12L component/byte ordering for multiple non-symmetric pixel patterns,
  block boundaries, source stride padding, and first/last pixels of a row.
- Scalar, SIMD, threaded, optimized, 720p-special, arbitrary-width, and DCI
  paths that implement the same conversion produce identical active samples,
  except for an explicitly documented and tested edge-concealment policy.
- Output-contract and media-type tests prove Alpha/libplacebo, MPC Video
  Renderer, and madVR receive range metadata matching the actual buffer.
- Representative SDR and HDR hardware captures validate black floor, white
  level, near-black/near-white ramps, legal excursions, neutral chroma, and no
  visible range change when switching between equivalent renderer paths.
- A clean x64 Release build and the complete relevant test suite pass.
  Conversion benchmarks show no material 4K60 throughput regression; any
  measurable cost and chosen trade-off are recorded.

## Acceptance criteria

- The range of every supported formatter input and output is explicit,
  testable, and propagated to its consumer; no active route depends on an
  undocumented full-range assumption or an unknown RGB48 output contract.
- Full-range R12B/R12L retain full-range scaling. Limited-range r210/R10b/R10l
  receive explicit input normalization or code-preserving widening rather than
  being silently processed as full range.
- Full-range samples, when selected by the established contract, map endpoints
  exactly with a documented monotonic scaling rule.
- Packed RGB-to-P010 produces the intended nominal range with correct matrix,
  offsets, rounding, clipping/excursion behavior, and matching renderer
  signaling for every supported packed RGB encoding.
- Existing UYVY/v210 limited-range preservation remains byte-exact unless a
  test and authoritative contract prove a current defect.
- Tests guard the correct R12B mappings `0x100 -> 0x1001` and
  `0xEB0 -> 0xEB0E`, while detecting the incorrect analogous behavior in
  limited-range r210; black/white endpoint tests alone cannot pass the story.
- Diagnostics allow a captured frame's range decision to be traced from
  DeckLink ingress through formatter output to renderer interpretation.

## Non-goals

- Do not infer range by histogramming live images.
- Do not change transfer functions, primaries, HDR metadata, tone mapping,
  LUT policy, or renderer-output calibration except where necessary to make
  range signaling truthful.
- Do not introduce a new frame copy, queue, worker transition, or per-frame
  allocation.
- Do not treat the user's DirectShow nominal-range override as proof of the
  DeckLink input range.

## Related stories and references

- VP-0009: Alpha renderer DeckLink R210/R12B format parity.
- VP-0023: Alpha P010 sample-range contract and regression tests.
- VP-0069-1: Native-format Alpha ingress and conditional P010 analysis.
- VP-0075: Alpha native RGB analysis parity.
- [Forum concern](https://www.avsforum.com/threads/videoprocessor.3206050/page-405?post_id=64738488#post-64738488)
- `src/VideoProcessor-Lib/video_frame_formatter/`
- `src/VideoProcessor-Lib/VideoState.h`
- `src/VideoProcessor-Lib/microsoft_directshow/video_renderers/`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- `docs/VP-0096_RANGE_CONVERSION_TEST_PLAN.md`

## Initial red-test inventory (2026-08-06)

The focused `VideoFrameFormatterTests` run built and executed from x64 Release:

1. `CDeckLinkR12BToP010UsesNormalizedFullRangeRounding`: current shift/clamp
   disagrees with normalized 12-to-10 rounding.
2. `CDeckLinkLimitedRgbToP010MatchesBT709ReferenceCodes`: current 10-bit packed
   RGB conversion treats limited inputs as full-range values.
3. `P010ConvertersUseOneVerticalChromaDownsamplingPolicy`: v210 selects even-row
   chroma while UYVY uses a rounded two-row average.
4. `CR210toRGB48VideoFrameFormatterGoldenTest`: r210 is widened using full-range
   replication rather than the test's candidate code-preserving alignment.
5. `DeckLinkR12BDirectMediaTypeUsesThirtySixBitsPerPixel`: returns 4, not 36.
6. `DeckLinkR210Rgb48ContractIsLimitedRange`: RGB48 output contract is unknown.
7. `DeckLinkPackedRgbFormatterContractsMatchDocumentedRanges`: the shared P010
   formatter always declares full range, including r210/R10b/R10l.

## Implementation checkpoint (2026-08-06)

- All seven red tests above now pass.
- R12-to-P010 uses exact normalized rounding backed by an immutable 8 KiB
  lookup table, reducing its measured 4K average from roughly 13.3-15.0 ms to
  roughly 10.0-11.0 ms.
- Limited r210/R10b/R10l BT.709 and BT.2020 reference vectors pass. r210-to-
  RGB48 preserves its 10-bit codes with a six-bit container shift.
- UYVY and all v210 P010 paths use the same rounded vertical chroma average;
  standard, optimized, threaded/SIMD, tail, and 720p-special tests agree.
- Alpha keeps R10b/R10l native with limited signaling and routes r210 through
  range-aware P010. DirectShow nominal range follows formatter output unless
  explicitly overridden. MPC/Generic forced P010 covers every supported input.
- Related native sparse-analysis math was corrected for limited 10-bit RGB and
  normalized R12, with reference-vector tests.
- Clean x64 Release build: passed. Complete test suite: 634/634 passed.
- The performance regression test now uses median-of-three runs, each with 120
  measured frames rotating across four patterned 4K buffers, and gates both
  average and p95 below 16.67 ms. Sustained averages were 2.2 ms for
  v210-to-P010, 4.6 ms for r210-to-RGB48, 5.6 ms for R12B-to-RGB48,
  11.0-12.7 ms for limited packed RGB-to-P010, and 9.8-11.1 ms for
  R12-to-P010. This exposed a 20-39% cost hidden by hot-buffer measurements
  for R10b/R10l; all median sustained p95 values remained below 14.4 ms.
