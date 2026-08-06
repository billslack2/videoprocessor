# VP-0096: Establish range-correct video-frame conversion contracts

## Status

In Progress (2026-08-06). Test-first range and routing analysis is active on
`codex/vp-0096-range-conversion-tests` in
`C:\Users\bslac\vp\vp-0096-range-conversion-tests`, based on the current
GitHub default branch `v1.1.016-beta` at `b6e2892`. Initial work is limited to
documentation review, route characterization, and tests that expose current
assumptions; no conversion policy change has yet been selected.

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

Bit replication is a full-code-range expansion. It is not neutral padding for
a limited-range signal. If a 12-bit limited-range code is meant to retain its
numeric video level in a 16-bit container, `value << 4` produces the exact
mapping: nominal black/white `0x100/0xEB0` become `0x1000/0xEB00`. The current
replication produces `0x1001/0xEB0E`.

Review of `b6e2892` found related assumptions that must be decided together:

| Path | Current behavior | Required review |
| --- | --- | --- |
| `CR12BtoRGB48VideoFrameFormatter` | Replicates 12-bit high bits into the low four bits | Establish captured R12B range and the RGB48 consumer contract; preserve limited codes or scale full-range values accordingly |
| `CR210toRGB48VideoFrameFormatter` | Replicates 10-bit high bits into the low six bits | Apply the same decision to r210; do not let the two RGB48 paths disagree |
| `CDeckLinkRGBToP010VideoFrameFormatter` | Treats R210/R10b/R10l/R12B/R12L as full-range RGB, rounds 12-bit components to 10 bits, performs a full-range RGB-to-YUV matrix, and declares full-range P010 output | Prove or correct the input-range assumption, 12-to-10 rounding, matrix/offset, clipping, legal-excursion, and output-signaling behavior for every packed RGB encoding |
| `CARGBtoP010VideoFrameFormatter` | Maps 8-bit RGB endpoints to 10-bit full range and declares full-range P010 | Verify that ARGB/BGRA ingress is contractually full range and keep it distinct from limited-range packed capture RGB |
| UYVY/v210 to P010/P210 formatters | Shift samples into the high bits and declare limited range | Use these as preservation controls; verify scalar/SIMD and special-width paths agree and do not clamp legal excursions |
| No-op and native Alpha ingress paths | May avoid a CPU conversion while still relying on range metadata | Verify that bypassing a formatter preserves the same input-range authority and renderer interpretation |

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
4. Correct `CR12BtoRGB48VideoFrameFormatter` and
   `CR210toRGB48VideoFrameFormatter` to implement the chosen limited/full
   policy. Add truthful output contracts and ensure the negotiated RGB48 media
   type and DirectShow nominal-range signaling agree with the bytes written.
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
- Limited-range preservation tests prove exact mappings such as 12-to-16
  `code << 4`, 10-to-16 `code << 6`, and existing 8/10-bit P010/P210 container
  alignment wherever preservation is the selected contract.
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
- Limited-range R12B and r210 samples, if supported by the established ingress
  contract, retain their exact nominal codes during precision/container
  widening rather than receiving replicated low bits.
- Full-range samples, when selected by the established contract, map endpoints
  exactly with a documented monotonic scaling rule.
- Packed RGB-to-P010 produces the intended nominal range with correct matrix,
  offsets, rounding, clipping/excursion behavior, and matching renderer
  signaling for every supported packed RGB encoding.
- Existing UYVY/v210 limited-range preservation remains byte-exact unless a
  test and authoritative contract prove a current defect.
- Tests detect the original `0x100 -> 0x1001` / `0xEB0 -> 0xEB0E` R12B issue
  and the analogous 10-bit behavior; black/white endpoint tests alone cannot
  pass the story.
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
