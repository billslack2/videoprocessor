# VP-0197: Configurable subtitle script hints for Alpha detection

## Status

In Progress (2026-10-11). Implementation is starting from the current GitHub
integration/default branch `v1.4.00-beta` at
`8186661b675b4c44b0f15882a4d90d4a3cbae03a`. Feature worktree created clean:
`E:\codex\videoprocessor\subtitle-script-preference-20261011`, branch
`codex/subtitle-script-preference-20261011`. No beta merge was requested. A local test deployment was
requested separately after the Release build.

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
   Release solution build succeeds. Local deployment is a separate request and
   does not replace these acceptance requirements.

6. Manually reproduce the user-supplied paused scene, with the CJK profile active and subtitle “她才是人民的物理学家” crossing the lower picture boundary. The detector produces a nonzero observed cue and a stable box on repeated frames while preserving the backing and picture-detail safeguards.

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

2026-10-11: Implemented the profile choice, validation, runtime settings, and
public configuration reference in the clean source worktree at
`E:\codex\videoprocessor\subtitle-script-preference-20261011` on
`codex/subtitle-script-preference-20261011`, based on `v1.4.00-beta` commit
`8186661b675b4c44b0f15882a4d90d4a3cbae03a`. General retains the existing
thresholds; Latin, Arabic, and CJK apply bounded spacing, connected-word,
detached-mark, and compact-line adjustments. Worker keys, bar references,
repeated-frame reuse, and cue continuity now include the selected hint, and a
live hint change clears prior cue history. Existing bright-core, backing,
centering, and bar-anchor gates remain in place.

`git diff --check` passes. The full x64 Release solution build completed
successfully with `MagewellSdkRoot` set to the workstation SDK at
`E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3`. The Config tests
project and main test DLL were compiled as solution outputs, but no test suite
was run. The source `AGENTS.md` now records the SDK path and build command.
No source commit or beta merge has been made. On 2026-10-11, the user separately requested
a local test deployment to `C:\Videoprocessor\vp`. The successful x64 Release
build from this worktree was deployed with matching runtime metadata:

- `VideoProcessor.exe`: `A65274AF8F1F0663F317A6015DE0117722E0817287A63BD2823675B118FA84A1`
- `vprenderer\VideoProcessorVPRenderer.dll`: `A72AB3E9D47A1F5C0BD05C4C065A918FF5E49505CE74612D312F840B47AE28D9`
- `config\VideoProcessorConfig.exe`: `820A2F4DD66298824397B141369EDAC6CA067183285F055361F8DA44CC8BD5EE`
- `config\VideoProcessorConfigDiscovery.dll`: `2FFC33BE6A0839968DBD9988897E343958EAE8B21E7F2583EE05AA5C237BCC31`

The previous binaries and metadata were backed up under
`C:\Videoprocessor\vp\deployment-backups\subtitle-script-preference-20261011-084304`.
The active `VideoProcessor.cfg` was not edited (SHA-256
`76C8AA1CA245679EC44A024A1288E441F81D26AC4B81A21D5EE94DC78094A7A3`). No test
suite or application launch was performed. Representative detector fixtures,
configuration persistence tests, and the focused suites remain open acceptance
work.

2026-10-11 manual scene feedback: the user selected Chinese / Japanese / Korean
and paused on a scene whose subtitle reads “她才是人民的物理学家”; they
reported no improvement over the prior result. The deployed runtime log for
that frame reports 22 sampled components, no cue, no measured bar gap, and
`no-backed-anchor` at analysis step 4. This narrows the failure to anchor
nomination before bar-gap measurement. Follow-up source change aggregates
sampled top/bottom bar ink across multi-component CJK text-core lines, while
retaining the centered-line and opaque-card checks, and distinguishes missing
centered text, missing bar-stroke support, and backing rejection in diagnostics.
The updated x64 Release solution build completed successfully. After the user
requested deployment, the player closed normally and the background Config
process was ended. The same-build runtime pair was deployed to
`C:\Videoprocessor\vp` and verified against the Release artifacts:

- `VideoProcessor.exe`: `72C60525F68EADBF6E749462F9719A07052CC83DBD7BA2614D053AA3E40624D7`
- `vprenderer\VideoProcessorVPRenderer.dll`: `982B94410200C2F9EFAC0BE97609FE0C4B87D3D312DEA6ED74E16905358388A8`

The previous pair is backed up under
`C:\Videoprocessor\vp\deployment-backups\subtitle-script-cjk-followup-20261011-093725`.
The active `VideoProcessor.cfg` was not edited; `subtitle_script_hint: cjk`
remains set and its SHA-256 is
`F4E1002AAE5346160FA4D3DBB032CD8460A1797D8E811B035FA85A2D2A3B8877`.
No test suite or application launch was performed. Manual scene validation,
representative detector/config fixtures, and focused suite runs remain open.

2026-10-11: At the user's request, restored the same x64 Release runtime pair
after the deployed files were overwritten. The overwritten pair was backed up
to `C:\Videoprocessor\vp\deployment-backups\subtitle-script-cjk-restore-20261011-102022`.
The restored host and renderer hashes match the original Release build hashes
recorded above. The active CJK configuration remained unchanged (SHA-256
`F4E1002AAE5346160FA4D3DBB032CD8460A1797D8E811B035FA85A2D2A3B8877`). No app
was launched and no tests were run.
