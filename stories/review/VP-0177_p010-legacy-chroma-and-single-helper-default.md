# VP-0177: P010 legacy chroma and single-helper default

## Status

Review (2026-09-10). Rebased commit `04d3988c` is built, tested, pushed and
deployed on the requested VP-0174 base `16a8102f`. See final evidence below.
Previous implementation commit `7a85f41b` pushed to
`codex/p010-legacy-chroma`; draft PR:
https://github.com/billslack2/videoprocessor/pull/85
Base reverified at `89d55ca5` on `v1.3.005-beta` before committing.

x64 Release solution build passed with zero errors (existing Qt packaging
warnings remain). Native suite: 1,107 passed and the new oracle fixture failed
because it used dimensions rejected by DisplayMode. Corrected the fixture to
supported sizes; both new oracle/configuration tests then passed. All 1,108
native tests have passing coverage across the full run and targeted rerun.
UI test "unrelated content remains exact" passed with LEGACY and one helper.
Source review and git diff --check passed. Deployed on user request below.

Pending: PR review/merge and user hardware validation of CPU usage/image
quality with conversion active. Existing helper spin-wait is unchanged.

## User story

Reduce v210-to-P010 CPU cost by defaulting to one helper and allowing the
historical alternate-row chroma policy through configuration only.

## Requirements

- `[directshow.conversion] chroma_downsampling: AVERAGE|LEGACY`.
- Omitted chroma policy remains AVERAGE: rounded adjacent-row average.
- LEGACY uses zero-based even-row chroma and skips odd-row chroma averaging.
- Omitted min_core_count and max_core_count are both 1. Existing explicit
  counts retain their meaning (helpers in addition to the calling thread).
- Preserve AVX2 acceleration, padded-width and edge-pixel correctness, and
  10-bit limited-range output. Apply policy consistently to scalar and SIMD.
- Keep these controls configuration-only; unrelated UI saves preserve them.

## Readiness review

Current GitHub default/latest beta is v1.3.005-beta at 89d55ca5. The user's
standing instruction authorizes that remote beta as the implementation base.
Clean worktree: E:\codex\videoprocessor\p010-legacy-chroma.
Branch: codex/p010-legacy-chroma. Existing converter has persistent helpers
and scalar/SIMD kernels; this work changes chroma selection, not synchronization.
Native formatter/config tests can verify pixel oracles, defaults, schema
validation and width/thread boundaries. Build x64 Release and run tests.
No deployment requested. CPU benefit requires subsequent hardware validation.

## Tracker audit

195 canonical files and 195 rows agree by ID/state; maximum root 0176 and
registry counts agree. Existing VP-0088 status uses lowercase "In progress"
instead of canonical capitalization; left unchanged as unrelated history.

## Validation plan

Compare AVERAGE and LEGACY against independent per-sample expected values
for all conversion methods, aligned/padded widths and threaded resolutions.
Verify missing, partial and explicit configuration, invalid policy rejection,
and preservation of manual controls through a UI save.

## Implementation and validation evidence

- Separate template kernels for AVERAGE and LEGACY avoid runtime per-pixel
  policy decisions and let Release eliminate the odd-row averaging work.
- All conversion methods preserve luma, limited-range 10-bit packing, padded
  row handling and output edges. Schema accepts AVERAGE/LEGACY case-insensitively.
- Default min/max helper counts are 1 with no hardware-based promotion.
  Explicit configuration values remain honored; no UI controls were added.
- Oracle tests cover widths 100/102/104/106/108/110 (all SIMD tail lengths),
  1280/1920/3840/4096, non-divisible threaded partitions, 1/2/8 helpers,
  unaligned destinations, output guards and unchanged packed input.
- Configuration tests cover omitted sections, partial settings, overrides,
  LEGACY, AVERAGE, invalid ALTERNATE rejection and legacy section compatibility.
- Build log: E:\codex\videoprocessor\p010-legacy-chroma\p010-release-final-build.log
- Native results: x64/Release/TestResults/p010-native-tests.trx and
  x64/Release/TestResults/p010-focused-tests.trx in the source worktree.
- UI result: p010-ui-preservation.log in the source worktree.

## Deployment (2026-09-10)

User explicitly requested deployment. Refreshed x64 Release solution build
passed with zero errors. Deployed commit 7a85f41b to C:\Videoprocessor\vp:
VideoProcessor.exe and vprenderer/VideoProcessorVPRenderer.dll as a matched
pair, plus config/VideoProcessorConfig.exe for the new schema and both
configuration reference copies. Verified every installed SHA256 against
its source and every backup against the previous installed file.

Backup: C:\Videoprocessor\vp\backup-before-vp0177-20260910-083425
Exact hashes are recorded in deployment.json in that backup directory.

No configuration edit was needed: user already had LEGACY, min_core_count=1
and max_core_count=1. Configuration hash verified unchanged. DirectShow
video_conversion remains NONE. VP and Config were closed; left closed.
Story remains Review pending hardware CPU/image validation and PR review.

## Requested local base correction

User requested the latest local VP-0174 branch. Selected
`codex/vp-0174-gamma-wording` at `16a8102f`; retained its untracked artifacts
and existing worktree. Rebased only the P010 commit onto that tip cleanly.
Rebuilding/testing the combined version before refreshing the authorized
deployment. VP-0174 includes the newer calibration/Color Output UI behavior.

## Final rebased validation and deployment

- User-selected latest local base: codex/vp-0174-gamma-wording at 16a8102f.
- Rebased source: 04d3988c71d6f610d78620d087aa8363e8501871. Clean rebase;
  git range-diff reports the conversion patch unchanged. Worktree clean.
- Clean x64 Release rebuild passed with zero errors. Initial incremental
  link failed with cached PDB corruption; clean rebuild resolved it.
- All 1,132 native tests passed (p010-vp0174-native.log / matching TRX).
- Four UI checks passed: unrelated manual content, Color Output migration,
  calibration LUT/HDR gamma ownership and SDR gamma label persistence.
- Draft PR #85 updated with an exact force-with-lease. Its beta-relative
  diff includes the local VP-0174 prerequisites; no merge performed.
- Refreshed the authorized deployment with the matched executable/renderer
  DLL, configuration editor and references. All installed hashes match the
  Release artifacts. Configuration hash unchanged (LEGACY, helpers=1,
  DirectShow conversion=NONE). VP and Config remain closed.
- Backup: C:\Videoprocessor\vp\backup-before-vp0177-vp0174-20260910-085709
  deployment.json contains exact before/after hashes for all six files.
- Remains Review for user CPU/image validation and review of combined PR.
