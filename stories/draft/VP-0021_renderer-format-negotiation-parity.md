# VP-0021: Renderer format negotiation parity and truthful capability reporting

## Status

Draft.

## Context

DeckLink format translation and the specialized renderer paths have gained
support for several raw formats, but the generic DirectShow renderer remains
inconsistent. Its media subtype mapping and automatic P010 selection do not
cover all formats that capture can select or that native conversion already
supports. In particular, ARGB/BGRA, R210, R12B, R10b, R10l, and R12L are not
handled uniformly. `VideoFrameEncodingBitsPerPixel` also has incomplete or
misleading values for some packed RGB formats.

This creates a risk of an incorrect media type, an avoidable renderer failure,
or a misleading claim that a format is supported.

## User story

As a user selecting a renderer, I want every renderer to either negotiate a
correctly converted format or state precisely why it cannot, so changing
renderer never silently changes pixel interpretation.

## Scope

- Establish one explicit capability table for raw input encoding, native output
  encoding, and DirectShow media subtype.
- Align generic DirectShow negotiation with supported native formatters.
- Make unsupported combinations fail early with actionable diagnostics.
- Correct format metadata helpers used for buffer sizes and media negotiation.

## Non-goals

- Do not redesign all renderers into one framework.
- Do not add a user-facing manual capture pixel-format preference.
- Do not add codecs or encoded-packet support.

## Implementation plan

1. Inventory each raw `VideoFrameEncoding` selected by DeckLink capture and
   identify its valid direct output and conversion outputs.
2. Create or extend a single narrow capability/mapping helper shared by
   DirectShow format negotiation paths; avoid duplicate switch statements
   where practical without broad refactoring.
3. Add generic-renderer P010 selection for every encoding with an existing
   tested P010 converter, including ARGB/BGRA and the packed RGB variants.
4. Add any missing DirectShow media subtype translations only when the
   formatter/output representation actually supports that subtype.
5. Correct `VideoFrameEncodingBitsPerPixel` and related stride/buffer helpers
   for R210, R10b, R10l, R12B, and R12L.
6. Add a startup diagnostic that names the source encoding, requested renderer,
   and valid alternatives when negotiation is intentionally unsupported.

## Verification

- Unit-test the capability table and every supported subtype mapping.
- Exercise generic and specialized DirectShow setup for UYVY, v210, ARGB,
  R210, R12B, R10b, R10l, and R12L using synthetic frames where hardware is
  unavailable.
- Confirm no media type is advertised unless its formatter and stride handling
  are available.
- Regression-test existing live UYVY/v210 DirectShow paths and queue behavior.

## Acceptance criteria

- Generic and specialized renderer paths use consistent, truthful format
  capability decisions.
- All formats supported by existing native converters either negotiate correctly
  or return a clear, specific unsupported-format error.
- Packed-RGB size/stride calculations are correct and tested.
- No timing, reset, or queue policy is changed by this story.
