# VP-0182: Config-only ADVANCED Lanczos chroma downsampling

## Status

Review

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

## Implementation and review evidence

Commit 0b13008e7cbb6f6e9eb037d5f68fba3012b0f6bd, branch
codex/p010-advanced-chroma; draft PR https://github.com/billslack2/videoprocessor/pull/90
against v1.3.005-beta. Existing AVERAGE/LEGACY kernel lines and defaults are
unchanged; config schema/parser adds ADVANCED. New scalar/AVX2 filter is separate.

x64 Release build passed with zero errors (existing compiler and Qt packaging
warnings). All 1,146 native tests passed. Independent analytic Lanczos oracle
covers Q14 accuracy within one 10-bit code, exact scalar/SIMD output, image edges,
all SIMD tails, UHD/DCI, source/output guards and 1/2/8 helper partitions.
Lifecycle coverage now includes 1,152 changing frames across all three policies.
Focused UI save test preserves LEGACY and ADVANCED without exposing a control.

Short same-machine 4K60 benchmark, one helper, 16 logical processors:
AVERAGE 2.115 ms mean / 1.07% normalized converter-host CPU;
LEGACY 2.037 ms / 1.07%; ADVANCED 3.732 ms / 3.71%.
All idle windows measured zero process CPU time. These are converter-only
measurements, not whole VP/madVR CPU or end-to-end playback guarantees.

Full filter specification, renderer routing evidence, 24/60 fps measurements and
limitations are in source docs/VP-0182-advanced-chroma.md. ADVANCED can ring and is
frame-based, not field-aware interlaced downsampling. User visual/hardware
acceptance and merge/deployment decisions remain pending; nothing was deployed
or changed in active configuration for this feature.
## Deployment 2026-09-10

On user request, deployed commit 0b13008e7cbb6f6e9eb037d5f68fba3012b0f6bd
from a successful x64 Release build (advanced-deploy-build.log, zero errors).
EXE and renderer DLL deployed together and SHA256 verified against build outputs;
updated config editor and CONFIGURATION.html copies deployed as well.
Backup: C:\Videoprocessor\vp\backup-before-vp0182-20260910-193636.
Its deployment.json records all old/new hashes and configuration hash.
EXE SHA256: 626D9ECB5FB6A1818D9DE31F54B3028F36BED89CE4D3D5D28418FC0EA2AE8BCA.
Renderer SHA256: E21F25EB18BCD88C8A9F7767E2F93F854BE7C466EAB12D2053FFEA8C63B6133F.
Active configuration was unchanged and already explicitly selected ADVANCED.
VP was closed and was not started. The existing running config-editor executable
was preserved under the backup as config/VideoProcessorConfig.running.exe;
reopen the editor to use the updated schema. Story remains Review pending visual
hardware acceptance and PR merge.
