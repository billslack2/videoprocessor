# VP-0009: Alpha renderer DeckLink R210/R12B format parity

## Status

Planned.

## Context

The experimental in-process alpha renderer is implemented in:

`src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`

DeckLink automatic input detection selects these raw formats:

- 8-bit YUV 4:2:2: `bmdFormat8BitYUV` / `VideoFrameEncoding::UYVY`
- 10-bit YUV 4:2:2: `bmdFormat10BitYUV` / `VideoFrameEncoding::V210`
- 8-bit RGB 4:4:4: `bmdFormat8BitARGB` / `VideoFrameEncoding::ARGB_8BIT`
- 10-bit RGB 4:4:4: `bmdFormat10BitRGB` / `VideoFrameEncoding::R210`
- 12-bit RGB 4:4:4: `bmdFormat12BitRGB` / `VideoFrameEncoding::R12B`

The existing DirectShow renderers support the selected R210 and R12B inputs with
native converters to RGB48:

- `video_frame_formatter\CR210toRGB48VideoFrameFormatter.cpp`
- `video_frame_formatter\CR12BtoRGB48VideoFrameFormatter.cpp`

The alpha formatter factory currently accepts V210, UYVY, ARGB/BGRA, R10b, R10l,
and R12L. It does **not** accept R210 or R12B, even though those are the packed
10/12-bit RGB formats selected by current DeckLink auto-detection. Consequently,
RGB 10-bit and 12-bit DeckLink input cannot run through alpha while it works in
the established renderer paths.

## User story

As a user of the alpha renderer, I want DeckLink 10-bit and 12-bit RGB input to
work exactly as it does with the established renderer paths, so selecting alpha
does not unnecessarily reject a valid HDMI RGB source.

## Scope

Add only alpha parity for the two actual automatically selected DeckLink RGB
formats: `R210` and `R12B`.

This is not a broad capture-format redesign and must not change DeckLink input
selection, DirectShow renderer behavior, capture timing, or alpha presentation
policy.

## Non-goals

- Do not add H.265 or DNxHR compressed-packet decoding.
- Do not add a manual pixel-format preference or expose BGRA/R10b/R10l/R12L as
  capture choices; automatic capture does not currently select those formats.
- Do not implement 12-bit YCbCr 4:2:2. The SDK has no corresponding raw
  12-bit-YUV capture pixel format, so that requires a separate conversion-policy
  design.
- Do not alter alpha scene-aware timing, queue behavior, or swapchain pacing.

## Implementation plan

1. Extend the alpha `CreateFormatter` factory in
   `LibplaceboVideoRenderer.cpp` to accept `VideoFrameEncoding::R210` and
   `VideoFrameEncoding::R12B`.
2. Implement alpha-native conversion to the existing P010 upload format:
   - R210 must preserve its documented packed RGB component order and range.
   - R12B must preserve SMPTE 268M packed-component interpretation, including
     the existing width/stride validation rules.
   - Use the same Rec.709/BT.2020 coefficient selection and limited/full-range
     assumptions as the existing alpha packed-RGB-to-P010 conversions.
3. Prefer extending `CDeckLinkRGBToP010VideoFrameFormatter` when the input
   layouts can be represented accurately there. Otherwise add narrowly scoped
   formatter(s); do not route alpha through DirectShow's RGB48 output merely to
   reuse those formatters.
4. Retain the existing alpha renderer's conversion performance reporting so
   R210/R12B appears in its normal conversion statistics.
5. Add deterministic unit tests for both formats, including:
   - black, white, and primary-color conversion;
   - 10-bit and 12-bit component expansion/downconversion;
   - stride/padding handling;
   - valid 4K-sized smoke coverage;
   - rejection of invalid dimensions required by the packed layout.
6. Improve alpha startup errors so an unsupported input format names the
   encoding and does not imply a generic libplacebo/GPU failure.

## Verification

- Build x64 Release and run the format-converter unit tests.
- With an R210 DeckLink RGB source, start alpha and verify successful renderer
  construction, correct RGB primaries/gray scale, stable queues, and nonzero
  conversion metrics.
- Repeat with an R12B source. Validate both SDR Rec.709 and BT.2020 signaling
  when hardware is available.
- Regression-test existing alpha UYVY, V210, and ARGB input paths.
- Regression-test R210/R12B with the established renderer paths; their behavior
  must be unchanged.

## Acceptance criteria

- Alpha accepts the same automatic DeckLink raw RGB formats as the general
  renderer path: R210 and R12B.
- RGB component order, bit depth, colorspace coefficients, and input stride are
  tested and correct.
- No unrelated capture format, timing, queue, or DirectShow code changes are
  required.
- The story is not marked Complete until build/test results and a real-source
  validation result are recorded in this Status section.
