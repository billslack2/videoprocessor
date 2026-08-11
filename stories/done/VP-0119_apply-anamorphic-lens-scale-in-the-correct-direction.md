# VP-0119: Apply anamorphic lens scale in the correct direction

## Status

Done (2026-08-11). Implemented in `faaff88` and merged to the default
`v1.2.001-beta` branch by PR #58 as merge commit `9ba40d4`. Live comparison
with madVR showed that VP Renderer widened a `16:9` source to `32:9` for a
configured `2:1` lens; the corrected renderer pre-compresses it to `8:9`.

Validation: a clean x64 Release rebuild passed; all 89 focused
`AlphaSourceCropPolicyTests` passed; all 34 configuration UI checks passed;
and the full native suite passed 793 tests with the same five pre-existing
configuration/reference failures and no new failure.

Live acceptance (2026-08-11): the operator tested the clean package built from
merged commit `9ba40d4` with the preserved `2:1` lens configuration and
confirmed that VP Renderer now produces the correct anamorphic geometry.

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
