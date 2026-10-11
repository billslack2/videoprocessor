# VP-0197: Configurable subtitle script hints for Alpha detection

## Status

In Progress (2026-10-11). Implementation is starting from the current GitHub
integration/default branch `v1.4.00-beta` at
`8186661b675b4c44b0f15882a4d90d4a3cbae03a`. Clean worktree:
`E:\codex\videoprocessor\subtitle-script-preference-20261011`, branch
`codex/subtitle-script-preference-20261011`. No beta merge or deployment was
requested.

## Problem

The active VP Renderer subtitle path scans sampled image pixels with
`SubtitleBoxDetector`; it does not recognize characters. Script structure can
change how words and marks form components: Arabic commonly joins letters and
places dots or diacritics apart from the main strokes, while Chinese, Japanese,
and Korean captions can use compact characters with little spacing. A language
choice for Windows OCR would not affect this active pixel-based path.

## User story

As a VP Renderer user, I want to choose the script style of the burned-in
subtitles, so the detector can group their visible marks appropriately while
keeping its existing safeguards against picture detail.

## Readiness review

- The current remote beta tip was verified on GitHub and fetched. The new clean
  worktree is at the exact remote tip; the existing checkout and worktrees were
  preserved.
- Unified configuration already stores selectable Subtitles profiles under
  `[vprenderer.subtitles.*]`. The Config editor builds this page from the
  existing profile UI and the runtime copies the selected profile settings into
  `RendererSettings`.
- Alpha subtitle measurements run asynchronously through
  `SubtitleMeasurementWorker` and `SubtitleBoxDetector`. Work results carry a
  policy generation and frame identity, so the selected hint can be added to
  measurement identity and stale work can be rejected across a live profile
  change.
- Existing component, grouping, centering, bright-core, black-backing,
  measured-panel, and bar-anchor checks provide bounded integration points.
  Tests can use the detector's synthetic sampled-pixel fixtures without an OCR
  engine or external runtime dependency.
- Prior playback-log review found Alpha's pixel detector active and no OCR
  fallback or DirectML activation. The implementation therefore belongs in the
  active detector path and must not be presented as selecting an OCR language.

## Required behavior

1. Add a `subtitle_script_hint` choice to the Subtitles profile with the UI
   labels `General / Mixed`, `Latin`, `Arabic`, and `Chinese / Japanese / Korean`.
   Store stable values `general`, `latin`, `arabic`, and `cjk`. An omitted value
   resolves to `general`; invalid values fail normal profile validation.
2. Pass the effective selection through profile resolution, live renderer
   settings, queued worker identity, and detector configuration. A queued result
   measured with an old hint must not be accepted after the profile changes.
3. `General / Mixed` must preserve the current detector behavior exactly for
   existing fixtures and omitted settings. Script-specific modes may tune
   bounded component, mark, and line grouping to reflect their spacing and
   attachment patterns; they do not perform OCR or identify characters.
4. Arabic grouping must retain connected words and permit finite detached dots
   or diacritics to join a supported line. CJK grouping must allow dense,
   compact characters with little word spacing. Latin grouping must retain
   ordinary word gaps and punctuation. A hint can improve grouping only where
   the selected mode changes a relevant detector decision.
5. Keep the existing bright glyph-core, opaque/black backing, centered-line,
   bar-anchor, component-budget, and measured-panel checks in force for every
   mode. Do not broaden thresholds globally or let picture texture such as the
   previously observed dark-jacket detail establish a subtitle cue.
6. Document that this setting guides pixel grouping and is not language
   recognition or an OCR language selector.

## Acceptance criteria

1. Config tests cover the default and all four choices, reject unsupported
   values, and prove save/reopen persistence in a Subtitles profile.
2. Detector fixtures cover Latin words and punctuation, an Arabic-like joined
   word with detached marks, and dense CJK-like compact glyphs. They demonstrate
   each script mode's intended grouping behavior and show unchanged General /
   Mixed results for the existing corpus. Fixtures are representative geometry
   cases; they do not claim broad language accuracy.
3. Picture-detail and dark-jacket negative fixtures remain rejected in every
   mode, including when bright fragments occur near a caption line. Existing
   bar, backing, and brightness safeguards remain covered.
4. Live profile changes invalidate or distinguish old worker results and do
   not combine cue history measured under different script hints.
5. The focused configuration and subtitle detector suites pass, and an x64
   Release solution build succeeds. No deployment is part of this story.

## Out of scope

- Selecting Windows OCR languages, DirectML changes, character recognition, or
  subtitle text extraction.
- Relaxing Alpha crop authority or accepting bright picture texture as a cue.
- Claiming accuracy for scripts, fonts, or languages beyond the included
  representative regression fixtures.

## Likely implementation areas

- `src/VideoProcessor-Lib/RendererProfileConfig.h`
- `src/VideoProcessor-Lib/SubtitleBoxDetector.h/.cpp`
- `src/VideoProcessor-Lib/SubtitleBoxLookahead.h`
- `src/VideoProcessor-Lib/SubtitleMeasurementWorker.h`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- `src/VideoProcessor-Config/ConfigEditorWindow.cpp`
- `src/VideoProcessor-Test/SubtitleBoxDetectorTests.cpp`
- `src/VideoProcessor-ConfigTests/ConfigEditorWindowTests.cpp`

## Tracker audit

Fetched `origin/main` before the audit. All 215 canonical root/child records
have exactly one matching index row; there are no duplicate IDs, missing files,
or unindexed records. The index count and next-ID registry match the records;
the highest root is VP-0196. Assigned VP-0197.

## Implementation progress

2026-10-11: Verified `v1.4.00-beta` at
`8186661b675b4c44b0f15882a4d90d4a3cbae03a`, created a clean worktree from that
tip, and traced the Subtitles profile to the asynchronous Alpha detector.
Implementation and focused regression coverage are in progress.