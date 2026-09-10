# VP-0181: P010 default maximum one helper

## Status

Backlog

2026-09-10: User requested a separate story and merge for the confirmed default.
Implementation already exists in PR #85; no additional source change is needed.
Pending clarification whether the requested merge includes that source PR.

## User story

As a VideoProcessor user, I want omitted P010 CPU settings to default to one
helper so conversion does not create extra workers by default.

## Acceptance criteria

- In [directshow.conversion], omitted max_core_count defaults to 1.
- Omitted min_core_count also defaults to 1; explicit values remain supported.
- Counts describe helper threads in addition to the calling thread, not a
  strict one-core CPU limit.
- Controls remain configuration-only and survive unrelated UI saves.
- AVERAGE remains the omitted chroma_downsampling default; LEGACY is opt-in.
- Both chroma policies use the blocking waits implemented in VP-0178.

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
