# VP-0181: Reduce P010 CPU use with blocking waits and configurable chroma

## Status

Review

2026-09-10: Work is implemented, tested and already deployed. User requested
this separate implementation story because we chose CPU improvements instead
of the proposed GPU converter, and authorized merging PR #85 into remote beta.
This story consolidates implemented work from VP-0177 and the CPU work within
VP-0178; those earlier records retain their detailed history. Hardware feedback
on full VP/madVR CPU and image quality remains pending.

## User story

Reduce v210-to-P010 CPU usage by parking idle helpers and the waiting caller,
defaulting to one helper, and supporting optional LEGACY chroma through config.
The GPU upload/compute/readback proposal was assessed but not implemented.
The separate madVR BT.2020-to-Rec.709 issue remains VP-0179.

## Acceptance criteria

- In [directshow.conversion], omitted max_core_count defaults to 1.
- Omitted min_core_count also defaults to 1; explicit values remain supported.
- Counts describe helper threads in addition to the calling thread, not a
  strict one-core CPU limit.
- Controls remain configuration-only and survive unrelated UI saves.
- AVERAGE remains the omitted chroma_downsampling default; LEGACY is opt-in.
- Both chroma policies park helpers between frames and block the caller while waiting for completion, without busy yielding/spinning.
- Shutdown/reload wake and join helpers safely; partial startup falls back to single-thread conversion.
- Preserve pixel correctness, padded widths, and existing explicit CPU settings.

## Existing implementation and validation

VP-0177 implemented the default in 04d3988c71d6f610d78620d087aa8363e8501871.
PR #85 head 6ca021b95e0862c335f6b3b218e7dc647514f97a also contains VP-0178.
https://github.com/billslack2/videoprocessor/pull/85

Source worktree: E:\codex\videoprocessor\p010-legacy-chroma.
Branch: codex/p010-legacy-chroma. User-requested local VP-0174 base: 16a8102f.
PR target: v1.3.005-beta; combined scope includes those VP-0174 prerequisites.

Existing evidence: successful x64 Release build, 1,134 native tests passed,
including configuration defaults/overrides and independent pixel oracles.
Focused UI preservation test passed. Head 6ca021b9 is already deployed with
verified EXE/renderer DLL hashes. This story does not change active config.
Full wait measurements: docs/VP-0178-p010-wait-validation.md in the source tree.

## Tracker audit

Before allocation: 199 canonical files and index rows, no duplicate/missing
IDs; registry maximum VP-0180, next VP-0181, total 199 agree. Existing status
capitalization variations are preserved outside this story.

## CPU validation results

Converter-only 4K60 harness on the same Ryzen 5700G, one helper: normalized
process CPU fell from 6.64% to 0.98% for AVERAGE and 6.62% to 1.22% for
LEGACY. Mean conversion stayed around 2.1-2.2 ms. Idle helper CPU fell from
about 6% per helper to zero measured in the short sample. These are not
whole-application CPU promises. Repeated wake/reload tests covered 768 changing
frames across 1/2/8 helpers. All 1,134 native tests passed.

## Merge evidence

PR #85 merged into remote v1.3.005-beta on 2026-09-10 at 15:09:34 UTC.
Merge commit: 30f36307dec1002a22a549599ee989fa6f0a971d.
Merged implementation head: 6ca021b95e0862c335f6b3b218e7dc647514f97a.
Story remains Review as requested, for user hardware feedback. No GPU converter
was added. No additional deployment or configuration change was necessary.
