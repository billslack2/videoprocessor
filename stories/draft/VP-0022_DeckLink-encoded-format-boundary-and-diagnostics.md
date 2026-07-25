# VP-0022: DeckLink encoded-format boundary and diagnostics

## Status

Draft.

## Context

VideoProcessor is a live raw-frame capture pipeline. DeckLink H.265 and DNxHR
are encoded-packet workflows, not ordinary raw `IDeckLinkVideoInputFrame`
formats. H.265 capture is delivered through the DeckLink encoder-input packet
callback and requires an explicit decoder pipeline before the existing frame
formatters or renderers can consume it.

The current code carries format identifiers for these modes, which can make the
application appear closer to supporting them than it is.

## User story

As an operator, I want unsupported encoded DeckLink modes to be identified
clearly at setup time, so I do not confuse a raw HDMI capture failure with a
missing codec pipeline.

## Scope

- Define the supported raw-frame boundary in code and user-visible diagnostics.
- Reject H.265 and DNxHR requests before normal raw-frame renderer setup.
- Record the future integration boundary for a deliberate encoder-input and
  decode project.

## Non-goals

- Do not implement H.265, DNxHR, file, network, or packet decoding.
- Do not add FFmpeg or another decoder dependency.
- Do not alter support for raw UYVY, v210, ARGB/BGRA, or packed RGB formats.

## Implementation plan

1. Identify all format-selection and renderer-start locations where encoded
   format enums can currently reach the raw-frame pipeline.
2. Add a single explicit encoded-format classification helper.
3. Reject those formats with a message that states they require the DeckLink
   encoder-input packet API and a decoder pipeline, rather than reporting a
   generic conversion or renderer error.
4. Ensure capability/help/config output lists only formats actually usable by
   the raw live-capture application.
5. Add focused tests for classification and diagnostics.
6. Document the future implementation boundary: packet callback, queueing,
   decode, timestamp ownership, and handoff into the existing raw-frame path.

## Verification

- Build x64 Release and run focused tests.
- Verify H.265/DNxHR requests fail before graph construction and do not leave
  capture or renderer threads running.
- Regression-test normal raw mode detection and renderer startup.

## Acceptance criteria

- Encoded DeckLink modes cannot be mistaken for supported raw-frame modes.
- Rejection is explicit, actionable, and occurs before partial startup.
- The application has no runtime FFmpeg dependency as a consequence of this
  clarification.
