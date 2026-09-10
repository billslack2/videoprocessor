# VP-0177: P010 legacy chroma and single-helper default

## Status

Backlog (2026-09-10). User approved implementation with the token LEGACY.

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
