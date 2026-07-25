# VP-0023: P010 range metadata contract and conversion regression tests

## Status

Draft.

## Context

Native RGB-to-P010 conversion intentionally converts full-range packed RGB to
full-range 10-bit YUV values. Renderer metadata can subsequently be automatic
or explicitly overridden to nominal limited range. That separation is valid
only when it is deliberate and tested; otherwise colors can appear subtly
incorrect while the conversion itself looks technically successful.

The removal of FFmpeg makes this native contract especially important: all
expected luma/chroma range behavior must be owned and validated in this code.

## User story

As a user switching renderer and color-range settings, I want P010 samples and
their declared range metadata to agree, so blacks, whites, and saturation do
not change unexpectedly between supported native input formats.

## Scope

- Document the sample-range contract for every native P010 formatter.
- Make the handoff between conversion range and renderer nominal-range metadata
  explicit.
- Add regression tests that distinguish full and limited range correctly.

## Non-goals

- Do not alter the user’s renderer nominal-range override semantics.
- Do not introduce color calibration, ICC/3D LUT, or tone-mapping work.
- Do not change capture timing or queue logic.

## Implementation plan

1. Inventory P010 formatters for v210, UYVY, ARGB/BGRA, and packed RGB inputs,
   recording input range assumptions, output code-value range, and color
   coefficient selection.
2. Define a single documented conversion contract in source comments and
   developer documentation.
3. Audit the metadata passed to DirectShow and the alpha renderer so automatic,
   full, and limited modes have unambiguous meaning.
4. Add deterministic code-value tests for black, reference black, white,
   reference white, neutral gray, and saturated primaries across representative
   RGB and YCbCr converters.
5. Add a lightweight integration test that asserts the selected nominal range
   metadata matches the requested renderer policy without modifying samples.

## Verification

- Build x64 Release and run all conversion tests.
- Compare full and limited test vectors against known expected 10-bit code
  values.
- Manually check a grayscale/range pattern through each renderer with AUTO,
  FULL, and LIMITED selections.

## Acceptance criteria

- Each native P010 formatter has a documented and tested sample-range contract.
- Renderer range metadata is not silently contradictory to the conversion
  output.
- Existing user-facing nominal-range overrides remain available and behave as
  documented.
