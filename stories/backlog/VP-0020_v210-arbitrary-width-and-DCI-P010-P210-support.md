# VP-0020: v210 arbitrary-width and DCI P010/P210 support

## Status

Backlog.

## Context

VideoProcessor supports raw 10-bit YCbCr 4:2:2 DeckLink capture as
`VideoFrameEncoding::V210`. The native v210-to-P010 and v210-to-P210
formatters currently require most widths to be divisible by six, with a
special-case path for 1280x720.

That restriction excludes otherwise valid DeckLink modes whose active width is
not divisible by six, including common DCI widths such as 2048 and 4096. v210
rows are packed in six-pixel groups and commonly include a padded final group;
the converter must consume the padded bytes safely while emitting only active
pixels.

## User story

As a live DeckLink user, I want valid v210 DCI and other non-six-aligned modes
to convert natively to P010 and P210, so I can use supported capture modes
without falling back to an external conversion library.

## Scope

- Extend the native v210 P010 and P210 converters to support any valid even
  active width accepted by the DeckLink v210 layout.
- Decode a padded terminal v210 group safely and crop output to the active
  width.
- Retain the native, allocation-free per-frame conversion design.

## Non-goals

- Do not change capture timing, queue behavior, or renderer negotiation.
- Do not add compressed H.265 or DNxHR decoding.
- Do not revive FFmpeg for v210 conversion.

## Implementation plan

1. Document and centralize the expected v210 row-byte calculation and required
   input alignment.
2. Replace the generic width-divisible-by-six rejection in
   `CV210toP010VideoFrameFormatter` and `CV210toP210VideoFrameFormatter` with
   a bounded tail decode that reads only bytes guaranteed by the source stride.
3. Keep the existing vectorized/main-group path for full six-pixel groups;
   handle at most one terminal group per row with a small scalar path.
4. Verify chroma siting and 4:2:2-to-4:2:0 vertical filtering are identical to
   the existing path for aligned widths.
5. Update converter error messages to distinguish invalid source stride from a
   valid padded tail.
6. Add deterministic tests for 2048x1080 and 4096x2160, plus at least one
   small synthetic non-six-aligned width with sentinel padding bytes.

## Verification

- Build x64 Release and run all converter tests.
- Prove identical output for existing aligned-width v210 test vectors.
- Verify sentinels immediately after each source row are never read.
- Perform 4K DCI P010 and P210 performance smoke tests; record average and
  maximum conversion time against the current 4K aligned baseline.

## Acceptance criteria

- Valid even-width v210 frames, including 2048 and 4096 DCI modes, convert to
  P010 and P210 without FFmpeg.
- Existing 720p, 1080p, and 3840-wide output remains byte-for-byte equivalent
  to the pre-change converter for existing test vectors.
- Tail handling is stride-safe and covered by automated tests.
- The implementation preserves the native fast path for complete six-pixel
  groups.
