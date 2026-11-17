# VP-0233: Research and improve subtitle cue reuse performance

## Status

Backlog

Requested 2026-11-16 after the user reported that the focused VP-0231 beta-based subtitle checkpoint looked correct during live playback. The current checkpoint is source commit `f7979b623fa66c735b682fd0404c3c45a567c093` on base `acdb89bfb5e1796cda6359aa2f89632b78204a5b`; its merge status must be checked on GitHub. After the initial positive subtitle check, the user reported a serious full-raster framing regression during Three Body Problem playback. The checkpoint is not accepted for merging; do not alter core aspect-ratio or crop logic as part of this story. Investigate any subtitle-side interaction separately and preserve the working picture. This story is for a focused subtitle performance improvement, not an aspect-ratio or framing redesign.

## User story

As a viewer, I want unchanged subtitles to remain correct while VP avoids repeatedly rebuilding the same cue, so live HDMI video keeps advancing smoothly without expensive subtitle work on the renderer thread.

## Evidence and the question to answer first

- Did an earlier version already avoid a full subtitle rescan for unchanged cues? Investigate the actual history, previous performance work, release packages and pinned source commits, including the earlier RC6 and pre-RC6 builds where subtitles worked well. Determine whether any previous optimization reused across distinct source frames or only reused buffers, samples, repeated presentations, or exact-frame results. Identify when and why any cross-frame fast path was removed, bypassed, or changed; do not assume it existed or was safe. Compare behavior, scheduling, buffering and measured cost under matched inputs before choosing an approach.
- At 23:32:34 on 2026-11-16, the checkpoint logged 120 subtitle worker jobs classified as full scans for 120 frames in five seconds, at approximately 23.976 source fps: 22.768 ms mean, 38.287 ms peak and 2.732 seconds worker busy time. Separately, renderer-side subtitle consumption averaged 2.611 ms and peaked at 5.002 ms. These are distinct timing scopes, not cumulative or end-to-end latency; the worker's `full_scans` counter classifies completed jobs and is not a detector-stage breakdown. GPU shader compilation is not what this CPU worker metric measures.
- Current source schedules a new measurement for every due source frame. Same-source-frame deduplication and `shared_samples` within a scan are not proof of reuse between different movie frames. Some current-pixel verification and state work still runs on the renderer path. Profile first, including actual grouping, current-pixel verification, backing/cleanup, and mandatory capture/render/present costs.
- The prior static-cue reuse prototype in `E:/codex/videoprocessor/subtitle-static-cue-reuse-20261116` was rejected: it could miss new picture-side text and lacked safe current-source proof through composition. It is evidence, not an approved fix. Preserve and learn from the user's successful live checkpoint rather than integrating unvalidated prototypes.

## Required work

1. Start from the current remote beta tip using the VP source-worktree rules. Find and document historical source pins, the exact previous mechanism and its cost, and the reason for any change. If it was removed, assess whether its limitations can be fixed without losing correct subtitles. Reuse existing proven behavior where appropriate rather than refactoring the entire subtitle system.
2. Make unchanged multi-frame cues cheaper: reuse verified text/layout/glyph information before full grouping, with bounded checks of current source pixels for existing and newly added or changed text, backing and cleanup. Moving picture background by itself is not a cue change. A changed or ambiguous cue must get fresh analysis. Keep results exact-source and context-bound; do not use old movie, background or cleanup pixels as current proof.
3. Move expensive current-pixel verification, grouping and template/cleanup preparation off the renderer thread on a bounded worker path. Do not wait for optional subtitle work or replay a previous whole frame. Preserve current source identity, timestamps and caption/picture pixels using the existing correct behavior and a current-source pixel-preserving fallback when a result is late or unavailable. Keep source cadence, deinterlacing and bounded latency intact; do not conceal delays with larger queues or lookahead.
4. Keep this work tightly coordinated with [VP-0232](VP-0232_move-subtitle-verification-off-renderer-thread.md), which owns the wider worker scheduling and verification chain, and [VP-0225](../in-progress/VP-0225_subtitle-processing-performance-and-repeatable-playback-stalls.md), which owns broader playback-stall investigation. Avoid duplicate implementations; record the ownership and integration decision before overlapping changes. Preserve the independently verified VP-0231 subtitle fix. Do not change aspect detection, picture geometry, cropping or fitting as a performance shortcut, or perform legacy-renderer-specific work.

## Acceptance and validation

- Document the historical answer with source commits, package pins and reproducible comparisons. If no previous cross-frame reuse existed, say so and distinguish it from within-frame or repeated-output caching.
- On representative real consecutive captured frames and matched playback, demonstrate materially fewer full groupings for unchanged multi-second cues and lower worker and renderer verification cost versus baseline. Report counts, mean, p95 and worst case, source/presentation gaps, queue age, and actual capture-to-present critical-path costs against the applicable frame budget; do not infer smooth playback from drop counters alone.
- Check changing and dense multi-line subtitles, added text inside and outside the old bounds, low-contrast/color text, moving background, changing backing and cue endings, volume/player menus and persistent overlays. Verify present current subtitle and picture pixels through composition, not only detector flags.
- Exercise slow, missing, failed and out-of-order worker results and profile, format, viewport and source resets. Demonstrate distinct eligible current-source video continues without waiting, replaying old frames or using stale proof. Cover Ben-Hur's missing-edge incident, true aspect transitions and Off-mode framing. Live HDMI testing must verify correctness and continuity; use only a completed x64 Release build when deployment is separately authorized.

## Tracker audit

At creation the canonical-state audit found 250 indexed items and 250 story files, no duplicate IDs or missing index/files, and highest root VP-0232; this new story uses VP-0233.
