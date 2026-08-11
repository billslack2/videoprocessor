# VP-0119: Apply anamorphic lens scale in the correct direction

## Status

In Progress (2026-08-11). Live comparison with madVR showed that VP Renderer
widens a `16:9` source to `32:9` for a configured `2:1` lens, while lens
compensation must pre-compress it to `8:9`. Implementation branch:
`codex/vp-0119-anamorphic-direction` from the current default branch.

## User story

As an anamorphic-lens operator, I want a configured lens ratio to produce the
same pre-lens geometry as madVR, so the physical horizontal expansion restores
the intended picture proportions.

## Scope

- Interpret `anamorphic_scale` as the physical horizontal lens expansion.
- Fit using `source_aspect / anamorphic_scale`, not multiplication.
- Apply the correction consistently to normal rendering, NLS fallback, and
  shader prewarming geometry.
- Correct tests and documentation that encoded the old direction.

## Acceptance criteria

- A `16:9` source with `2:1` produces a pre-lens aspect of `8:9` (`0.888889`).
- A `1:1` value remains a geometry no-op.
- Existing supported decimal and ratio parsing remains unchanged.
- Focused geometry tests and a clean x64 Release build/test run pass.

