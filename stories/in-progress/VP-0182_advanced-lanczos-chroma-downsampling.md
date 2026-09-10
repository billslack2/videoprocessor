# VP-0182: Config-only ADVANCED Lanczos chroma downsampling

## Status

In Progress

2026-09-10: User authorized adding ADVANCED while leaving AVERAGE/LEGACY and
AVERAGE default unchanged. Existing implementations will not be rewritten.

## Scope and acceptance

- Add ADVANCED to [directshow.conversion] chroma_downsampling; config only.
- Use scale-aware Lanczos-3 vertical filtering for 4:2:2 to 4:2:0 decimation,
  centered between each existing row pair. Twelve source rows, clamped borders,
  normalized signed fixed-point coefficients; round and saturate to 10 bits.
- Preserve luma, AVERAGE/LEGACY pixel kernels, omitted defaults and UI behavior.
- SIMD and scalar must agree, including padded widths, edges and helper boundaries.
- Reuse blocking helper pool; no per-frame heap allocation for the new filter.
- Document CPU/quality trade-offs and confirm VP Renderer conversion selection.

## Readiness

Queried remote default/beta v1.3.005-beta and fetched a33c5bf3. User standing
instructions authorize this base. Clean worktree under E:\codex\videoprocessor\
p010-advanced-chroma, branch codex/p010-advanced-chroma. No deployment or merge
of this new feature requested. The formatter has one caller and per-helper
output partitions; source halos are read-only across partition boundaries.

## Renderer findings

CreateAlphaFormatter in LibplaceboVideoRenderer.cpp uses v210-to-P210 when P010
is not explicitly requested, preserving 4:2:2 chroma. Explicit P010 selects the
same v210-to-P010 formatter, which loads directshow.conversion settings despite
the historical section name. ADVANCED does not change renderer selection.

## Validation plan

Retain existing AVERAGE/LEGACY oracles unchanged; add independent analytic
Lanczos reference tests, constant/edge/impulse and alternating chroma, all methods,
padded widths, guarded unaligned output and multi-helper frame partitions.
Verify config schema/defaults and UI saves; build x64 Release and benchmark
4K24/60 ADVANCED vs AVERAGE/LEGACY. High-order ringing is possible, not a promise
of universally better images. Visual hardware acceptance remains separate.

## Tracker audit

200 files/index IDs matched before allocation, no duplicate or missing IDs;
registry max 0181 and next 0182 agree. Unrelated capitalization retained.
