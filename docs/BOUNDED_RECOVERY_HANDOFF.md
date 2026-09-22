# Bounded recovery across a changed crop

## Reproduction

Local build `6e573c0511850af267d9615c4f9f512c0caad067`, 2026-09-22:

- At 14:18:40 (generation 2, frame 6542), and again at 14:18:50
  (frame 6796), buffered proof accepted picture bounds `0,68-3840,2092`.
- Current retention was refreshed for those bounds: valid, excluded bands
  safe, no outward content, and `near_black=0`.
- Recovery already displayed the wider safe envelope `0,54-3840,2106`.
  Its saved logical crop was `0,276-3840,1884`.
- Changing that logical crop set `trusted-contract` and discarded the envelope.
  With no content outside the new crop, there was no new outward envelope;
  recovery therefore displayed full raster instead of retaining its safe bounds.
- Full raster lasted 10 source frames (about 417 ms) in each replay, until
  scope returned on frames 6552 and 6806.

The missing case is a geometry handoff during recovery. This code predates
this build's subtitle handoff; the logs do not establish when the behavior was
introduced. The near-black episode is a separate policy and is unchanged.

## Correction and limits

Keep the saved presentation envelope across a changed logical crop only when
fresh evidence for this source frame proves all excluded bands of the entire
new crop safe, and the envelope contains that crop and its current observation.
Require matching source/epoch/raster, trusted observations, chroma-aligned valid
bounds, current retention identity, a verified current crop candidate, and a
non-repeated, non-near-black frame outside moving/near-black presentation modes.

The saved envelope grants no new crop authority. Final crop admission remains
unchanged. Reset logical recovery proof when the contract changes, then require
the existing adjacent-frame dwell before any inward return. Invalid evidence
retains the existing full-raster fallback. Real full-raster authority still wins.

## Validation

`artifacts/lookahead/bounded-contract-red.trx` records the exact regression
failing against the unchanged recovery implementation. The replay includes final
presentation admission, both reported sequence positions, old partial votes,
and a subtitle observation inside the new larger crop during recovery.

Independent QA added 28 invalid-evidence/envelope cases, repeated/older source
frames, context resets, true full-raster authority, and final-admission protection.
`bounded-contract-adversarial-red.trx` recorded four passes and one additional
failure: a changed crop inherited three old proof votes on a repeated frame.
The correction now clears votes before cadence handling and preserves the
`proofReset` diagnostic through subsequent evaluation.

`artifacts/lookahead/bounded-contract-green.trx`: all 1,420 native tests passed,
including all five new regression methods. Tests exercise final displayed bounds
and final crop admission, not merely detector state. Both independent code
reviews found no remaining blocker after the vote reset correction.

This change still requires local playback validation. It has not been deployed;
the current deployed baseline remains `6e573c05`. No detector thresholds,
near-black policy, lookahead configuration, or recovery duration were changed.

Full x64 Release solution build passed; see artifacts/lookahead/bounded-contract-release-build.log.
