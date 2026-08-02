# VP-0023: Alpha P010 sample-range contract and conversion regression tests

## Status

Done 2026-08-02. Merged and pushed to `v1.1.015-beta` at `44e3099`
(`VP-0023 log initial Alpha formatter contract`).

Verification is complete:

- x64 Release build succeeded and `VideoFrameFormatterTests` passed 38/38.
- Live Alpha diagnostics for 3840x2160 v210 reported P010 limited-range,
  10-bit samples with shift 6, matching libplacebo's limited source levels.
  Active luma was 64..940 and chroma stayed within legal limited-range values.
- The user completed a visual Alpha smoke check without a visible issue.
- The temporary `output_diagnostics` setting was removed, restoring the active
  configuration exactly to its pre-test contents.

DirectShow/madVR format/range negotiation remains deferred to VP-0021.

Implementation 2026-08-02:

- Added an explicit formatter output contract: sample range, source colour
  depth, and P010/P210 storage shift. Alpha uses it for libplacebo levels and
  bit metadata, removing its duplicate encoding-based range switch (including
  the prior R210 omission).
- Corrected 8-bit ARGB/BGRA-to-P010 conversion to map full-range endpoints
  exactly to 0/1023 rather than truncating white at 1020.
- Added exact code-value tests for contracts, RGB endpoints/Rec.709 red,
  limited HDYC reference black/white/neutral, and the 720p v210 edge mask.
  `VideoFrameFormatterTests` passes 38/38; x64 Release Alpha renderer builds.
- Added once-per-contract Alpha logging. With `output_diagnostics=true`, it
  also logs one pre-upload luma/chroma min/max scan and excludes the two
  deliberately concealed 720p edge columns. Existing output readback remains
  the downstream corroborating telemetry.

## Context

Alpha normally uploads supported RGB sources directly and v210/UYVY sources
through lossless P210. P010 remains an explicit conversion choice, a fallback
for sources without a qualified native path, and the established reference
representation for P010-oriented analysis.

Native RGB-to-P010 conversion intentionally converts full-range packed RGB to
full-range 10-bit YCbCr values. The v210 and UYVY converters preserve their
source YCbCr code-value interpretation, which is nominally limited range for
the supported capture contract. Alpha must declare the selected formatter's
actual result through libplacebo `pl_frame.repr.levels`; it must not derive a
possibly different range from a duplicated source-format list.

The removal of FFmpeg makes this native contract especially important: all
expected luma/chroma range behavior must be owned and validated in this code.

The v210 720p path deliberately conceals unreliable raster-edge pixels with
black luma and neutral chroma. Those pixels are a VP masking policy, not source
range evidence. Range diagnostics and preservation assertions must exclude the
concealed region, while separate tests lock down its exact geometry and fill.

## User story

As an Alpha-renderer user selecting or falling back to P010, I want converted
samples and their libplacebo range declaration to agree, so blacks, whites,
and saturation remain correct for every supported native input format.

## Scope

- Document the sample-range, precision, matrix, and chroma-downsampling
  contract for every P010 formatter Alpha can select.
- Make the handoff from the selected formatter to Alpha's
  `pl_frame.repr.levels` explicit and shared rather than inferred separately.
- Add deterministic regression tests that distinguish full and limited range
  and verify exact P010 code values.
- Add bounded Alpha diagnostics that report source encoding, selected ingress
  path, formatter output contract, and declared libplacebo levels once per
  renderer/source generation.
- Preserve and separately test intentional 720p/padded-edge concealment.

## Non-goals

- Do not change DirectShow media types, `DXVA_NominalRange`, madVR, MPC Video
  Renderer, EVR, or generic DirectShow negotiation. VP-0021 owns that work.
- Do not treat Alpha `output_range` as source/P010 metadata; it remains an
  independent display-output policy.
- Do not redesign Alpha's direct-RGB or lossless-P210 ingress paths.
- Do not add R12B Alpha parity; VP-0009 owns the remaining R12B work and must
  adopt this contract when implemented.
- Do not add arbitrary-width/DCI v210 P010 support; VP-0020 owns that extension.
- Do not introduce color calibration, ICC/3D LUT, or tone-mapping work.
- Do not change capture timing or queue logic.

## Implementation plan

1. Inventory every Alpha P010 route for v210, UYVY/HDYC, ARGB/BGRA, R210,
   R10b/R10l, and R12L. Record why P010 was selected, the source range
   assumption, output code-value range and precision, matrix coefficients,
   chroma siting/downsampling, and any intentionally concealed pixels.
2. Define one formatter-output contract in code and developer documentation.
   Alpha's upload metadata and diagnostics must consume that contract rather
   than repeat encoding-based range switches.
3. Define full-range RGB conversion as spanning the 10-bit P010 endpoints:
   black 0 and white 1023, with neutral chroma 512. Normalize 8-bit RGB into
   that domain with deterministic rounding instead of stopping at code 1020.
   Preserve legal limited-range YCbCr reference codes without range expansion:
   luma 64..940 and chroma 64..960, with neutral chroma 512.
4. Add deterministic code-value tests for black, reference black, white,
   reference white, neutral gray, and saturated primaries for Rec.709 and
   BT.2020 across representative RGB and YCbCr converters.
5. Exercise every selectable v210 implementation, including the 720p special
   path, and prove common active pixels produce the same code values. Test the
   intentional edge mask separately: exact concealed columns, luma 0, and
   chroma 512. Exclude those pixels from source-range statistics.
6. Add a lightweight Alpha integration test proving formatter selection,
   output contract, and `pl_frame.repr.levels` agree without a second range
   transformation.
7. Log the resolved Alpha ingress/range contract once per renderer/source
   generation. When output diagnostics are enabled, include bounded pre-upload
   P010 statistics that distinguish active samples from concealed edges.

## Verification

- Build x64 Release and run all conversion tests.
- Compare full and limited test vectors against known expected 10-bit code
  values.
- Run Alpha integration tests for forced/fallback P010 selection and confirm
  the declared libplacebo levels match the formatter output contract.
- Feed controlled full- and limited-range grayscale/PLUGE patterns through
  Alpha's P010 path and record the pre-upload contract/statistics plus the
  existing optional output diagnostic readback.
- Perform one final Alpha visual smoke check. Viewing is corroboration, not the
  primary proof of code-value or metadata correctness.

## Acceptance criteria

- Each P010 formatter Alpha can select has a documented and tested sample-range
  contract, including precision, matrix, chroma behavior, and intentional edge
  concealment.
- Alpha derives `pl_frame.repr.levels` from the selected formatter output
  contract, and tests prove the metadata does not contradict the sample values.
- Full-range RGB black/white map to P010 codes 0/1023; limited-range YCbCr
  reference black/white remain 64/940; neutral chroma remains 512.
- Runtime diagnostics identify the Alpha ingress path, output range contract,
  declared levels, and concealed-edge exclusion without per-frame log noise.
- Alpha direct-RGB/P210 selection, output policy, timing, queues, and analysis
  behavior do not change except where explicit P010 correctness requires it.
- No DirectShow or madVR behavior changes in this story.

## Dependencies and follow-up

- VP-0069-1 (Done) supplies Alpha's native-RGB/P210 ingress selection and the
  current explicit/fallback P010 baseline.
- VP-0021 owns later DirectShow/madVR format and range-negotiation parity and
  may reuse the formatter contract established here.
- VP-0009's remaining R12B Alpha work must implement this contract if it adds a
  P010 fallback.
- VP-0075 uses the qualified P010 path as range/reference evidence for native
  RGB analysis parity.
