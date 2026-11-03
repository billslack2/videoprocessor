# Subtitle pixel variation repair, 2026-11-03

## Source and evidence

Work starts from GitHub default/current beta `v1.4.02-beta` at `a591ad3b532771911e85f8ef026958279f2065f0`. Integration commit `3ac0e718` explicitly preserves the prior combined trial dependencies through `08f16516`; it does not treat an existing checkout as the baseline. The previous worktree and its unfinished edits remain intact. Deployed source is `69c51f18`; no runtime/configuration change is part of this implementation.

Private capture `analysis-20261103-161016-57848-1` contains 96 V210 UHD frames sampled every five sources, with four byte-distinct full rasters repeated 24 times. Caption luma differs by at most four 10-bit codes. During the Reconstruction log segment, retained native bounds remain `[0,274,3840,1884]`; the presentation oscillates with subtitle preparation. Captured source pixels are not checked into Git.

## Coordinated design

1. Keep an immutable cue reference. Use a normalized 10-bit Y/U/V difference budget of eight codes against previously verified footprint samples. A threshold category flip within this numeric envelope is not by itself a structural glyph change. Near-black cleanup also stays within a neutral near-black envelope; destination edge equivalence requires both endpoints to match the original samples. Do not cumulatively refresh the reference until arbitrary changes become acceptable. Preserve the actual CURRENT black classification and partial-evidence limits. Materially changed glyph cores, new text outside verified ownership, excessive chroma changes, real backing changes, and context changes remain rejection conditions. This is a defined small error envelope, not a claim to distinguish an intentional sub-envelope edit from encoding noise.
2. Identify encoded bars by physical source coordinates against the context-bound accepted native base. Subtitle search grids may insert a border row; the row at y=275 is picture when native top=274, even if the subtitle scan estimates top=276. Do not reinterpret picture pixels as bar ink, change physical detection, or force any screen aspect.
3. Keep accepted caption placement within its existing extraction margins across one-cell sampled line-box variation. A caption in an encoded bar need not have an opaque backing-card rectangle. Absence uses only the all-zero sentinel; malformed or unrelated nonempty rectangles reject. Keep exact CURRENT certificate identity/geometry matching after acquisition.
4. Do not extend video queues, timeouts, holds, or lookahead. Optional worker work remains bounded and separate from mandatory current-source video. Never reuse old movie/background/cleanup pixels. Missing results continue through the current-source fallback.

## Validation contract

- Regress the hard cleanup failure at source `(1274,1742)`, seed luma80/current84, and picture-edge false bar ink at y275.
- Cover permitted noise plus rejected +9 delta, cumulative drift, colored excursions, real new bar ink, changed/missing/new caption glyphs, invalid backing metadata, and context/profile/format resets.
- Run the same private replay before/after with actual sidecar geometry and source identities. Exercise every captured phase and distinguish actual five-source sampling from synthetic consecutive timing controls.
- A held-only certificate is not a general pass metric: ordinary nonheld acceptance intentionally has no held certificate. Assert final current prepared-erasure eligibility and fixed presentation across phases.
- Run the required Native suite once, then matching x64 Release runtime and both shipped renderer regressions, including slow/missing optional work and actual source progression. Report CPU-only/synthetic evidence separately from GPU presentation and live acceptance. Do not claim that zero drops proves uninterrupted playback.

## Historical context

Existing provenance investigation identifies shipped RC6 application pins `a8f62855` and `c716bd8` (the label/tag was reused); see `VP-0225_PENDING_SUBTITLE_CURRENT_SOURCE.md`. The expensive native-footprint cadence guard is repair-history code absent from those shipped pins. Its additional pixel preservation checks must be bounded and useful; current-frame correctness cannot depend on exact encoded pixel equality. A matched historical timing comparison is separate from capture-specific regression proof.

## Results

The first coordinated candidate `7d99bb85` passed all 1,088 Native contracts. The private new-capture timeline improved from 48/96 to 96/96 modeled prepared-admission successes, but the immutable reuse matrix still passed only 92/374: cleanup rejection exposed additional glyph/destination threshold flips. That candidate is incomplete and not deployed. Follow-up implements the consistent bounded-sample policy above, with separate tests. Final run receipts are stored under the ignored `artifacts/subtitle-tolerance` and `artifacts/qualification/runs` paths and summarized in authoritative VP-0225 tracker evidence. CPU modeled composition is not proof of actual GPU output or live frame cadence. No deployment or user acceptance claimed.
