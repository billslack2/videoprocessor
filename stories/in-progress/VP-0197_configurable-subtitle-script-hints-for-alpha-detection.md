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

2026-10-11 Config UI correction: after the runtime restore, the user reported
that Config still lacked the new script-hint option. The installed Config EXE
and discovery module hashes differed from the successful Release outputs, while
the active `VideoProcessor.cfg` still had `subtitle_script_hint: cjk`. Restored
both Config files from the same x64 Release build and verified them:

- `config\VideoProcessorConfig.exe`: `820A2F4DD66298824397B141369EDAC6CA067183285F055361F8DA44CC8BD5EE`
- `config\VideoProcessorConfigDiscovery.dll`: `BD111136D52BB4F54E3EDE5124569E3B81480D6BE31D9A5863A76509C2DAF95C`

The previous Config files are backed up under
`C:\Videoprocessor\vp\deployment-backups\subtitle-script-cjk-config-restore-20261011-102338`.
No configuration file was changed; the active CJK config hash remains
`F4E1002AAE5346160FA4D3DBB032CD8460A1797D8E811B035FA85A2D2A3B8877`. Config
was not launched for visual verification.

2026-10-11 CJK letterbox reproduction: the user paused on a second scene with CJK selected; the subtitle reads “我现在已经彻底地醒悟了”. The live detector trace found 34 components but produced no cue, with `analysis_reason=backing-card-rejected` at analysis step 4. The preceding aggregate bar-stroke change reached anchor nomination, but the opaque-card test rejected this line. Added a narrow CJK letterbox backing proof: require a centered line to cross exactly one verified physical picture edge, at least six aggregate edge-ink samples, at least nine non-glyph samples inside the actual letterbox bar, at least 80% dark samples, and dark coverage in at least two of three width tiles. The opaque-card proof remains the primary path. Backing rejections now report whether the interior, sample count, darkness, or distribution failed.

The complete `VideoProcessor.sln` x64 Release build succeeded using the workstation Magewell SDK. The user had previously requested a test deployment; the updated build is now installed at `C:\Videoprocessor\vp`. Verified hashes:

- `VideoProcessor.exe`: `2E2F226B7B56490973C33C6A0BA1CF481851E27C5A1411A672655C07D98B2BA7`
- `vprenderer\VideoProcessorVPRenderer.dll`: `E2598FD7C5F82062BE0487D7E897980086BFB0AE99D505640BFEDCB9767A4143`
- `config\VideoProcessorConfigDiscovery.dll`: `D8AE0DD87D30EE0FCC744E4CCAE5A17462F1DC10EDAB6D548EB821E78B341873`
- `config\VideoProcessorConfig.exe` already matched the same build and remains `820A2F4DD66298824397B141369EDAC6CA067183285F055361F8DA44CC8BD5EE`.

The replaced binaries are backed up under `C:\Videoprocessor\vp\deployment-backups\subtitle-script-cjk-letterbox-20261011-104239`. Neither active config nor profile state was edited; `subtitle_script_hint: cjk` remains selected in Reconstruction. Their verified SHA-256 hashes are respectively `CADDB0EA12678EF88EFD66EF39F046451A9FEC66E0DA367CCC85C91B57188E71` and `853C7F67FD7460070E5655FF53C6E07ED7C6FA97A61E997EE84F7074325BA01C`. The player and Config were not running at deployment time. After deployment, the player was launched for user testing (its window title reports this feature branch and base commit); the displayed frame state was not visually confirmed. No tests were run. Manual scene validation and focused detector/config suites remain open.
2026-10-11 two-line CJK investigation: the supplied before/after frames and native log showed only the lower row being extracted, with no source-card cleanup. Unowned disconnected radicals inside an established CJK row were omitted from ownership, and other accepted components inside overlapping glyph envelopes were counted as bright background. Added bounded ownership of locally backed contained radicals and made the card-interior check recognize every component owned by the same row. The x64 Release solution rebuilt successfully. The subtitle-focused suite on this detector revision passed 407/407, including the new disconnected-radical fixture across scales and sampling phases with negative scene interiors. Screenshot replay improved the large single line from 4/12 to 12/12 and the two-line scene from 0/12 to 10/12 (all six ordinary-orientation cases); the earlier small-line screenshot still fails, and two vertically mirrored two-line cases lose a terminal character. These limitations remain open.

Deployed the two-line detector runtime pair at 11:06 to C:\Videoprocessor\vp, backed up under deployment-backups\cjk-two-line-ownership-20261011-110607. Host SHA-256 C1D84916DAA1DE0C01FEF9F9E35DFA209908351847807041422A5645BD6E18A7; renderer E5376EC50F143E024B0FB30AE34E1FF5402632432BEB4743FB5954EE45EB4707. Native log frame 735 then reported two rows, box 1398,1734-2446,2018, with accepted cleanup 1342,1690-2502,1898. Configuration was preserved (CADDB0EA12678EF88EFD66EF39F046451A9FEC66E0DA367CCC85C91B57188E71).

The user subsequently reported constant Chinese flashing. Logs showed 34 pending previews in 68 rendered frames and zero successful pending recoveries. Added source-gap accounting to the current-pixel fallback and explicit pending failure diagnostics. Built x64 Release and deployed at 11:15; backup deployment-backups\cjk-pending-continuity-20261011-111521. Host 1DA2898CDFEA985C8B2D0C1E30A5B9F2997AE50B03B9BF7E34B9E67563ECB7C5; renderer 886BD3B6BD668B8362CDD292B742966B88F42C49E989A37F1B1D364EBDEB10BE. Configuration hash remained unchanged. No additional automated tests were run on this revision. Follow-up diagnostics showed pending-context-changed: queue retirement increments the active-picture continuity epoch and discards otherwise valid independent measurements of the exact still-current frame. A further correction preserves only independent same-frame scans under matching shared picture authority; old future/temporal votes remain invalidated. This correction is still being built/qualified.

The user then selected Arabic and supplied a paused scene showing incomplete glyph extraction. Live detection is stable, but the measured glyph box is only 1782,1918-2182,1994 and line capture 1787,1900-2174,1984, excluding part of the source sentence. Replaying the smaller source screenshot captures the full line, so native input diagnostics are being added to reproduce the exact sampling failure. Story remains in progress; no source commit, beta merge or completed manual qualification is claimed.


2026-10-11 native Arabic correction: added opt-in one-shot native input diagnostics (VP_SUBTITLE_CAPTURE_PATH, documented in AGENTS.md). The captured 3840x2160 V210 source, stride 10240, picture bounds 262-1898, reproduced the incomplete box 1782,1918-2182,1994 exactly. All 13 components passed local backing. Arabic ascenders and descenders were split into overlapping rows because their component bottoms exceeded the Latin baseline tolerance. Arabic now permits a half-height baseline difference while retaining vertical overlap, bounded union height, local backing, color and row-gap checks. Exact native replay now reports one complete row: box 1638,1882-2210,1994, glyph bounds 1646,1882-2200,1984, measured card 1584,1832-2260,1901. Native luma inspection independently places all visible sentence ink, including upper marks and the terminal period, inside those glyph bounds.

The final complete x64 Release solution rebuild succeeded. It also preserves independent exact-frame measurements across queue-retirement continuity epochs, discarding old temporal/future votes and retaining frame identity, script, policy and shared picture authority checks. No automated suite was rerun for the later continuity/Arabic revisions; the earlier 407-test result applies to the earlier CJK detector revision only. Deployed and verified the runtime pair, then reopened VP with diagnostic capture disabled. The user's current Arabic selection and display values were preserved; no configuration edits were made.

Backup: `C:\Videoprocessor\vp\deployment-backups\arabic-glyph-grouping-20261011-113100`. Configuration SHA-256: `49A1A7188F78E628772E97E0699083E499151C50A09245395E6EEFE891998306`.

- `VideoProcessor.exe`: `800A95C774EF1C1EA416094C9C5DC552E796DD8E860560E8EBFCE9DB80CE3A24`
- `vprenderer\VideoProcessorVPRenderer.dll`: `09F4AA39CBA31ABA42A7A442EF68FD0CB8A4A26444F62F8DFCEFF0F8FC300B55`

Final live confirmation: frame 126 acquired the complete Arabic row; frames 240 and 360 retained the same cue and box 1638,1882-2210,1994. Extraction is 1641,1879-2202,1984, accepted cleanup 1578,1826-2266,1898. At 11:31:28, the five-second window reported 120 frames with zero pending previews; cue 3 had 235 observations. Startup still had temporary pending previews. This confirms full bounds and stable capture for the paused native Arabic scene in logs, not broad visual qualification. User visual review, Chinese behavior under the original load, the earlier small CJK scene and mirrored terminal-character coverage remain open. No source commit or beta merge was performed; story stays in progress.

2026-10-11 follow-up: user reports remaining problems with subtitles containing only 1-3 characters and explicitly clarifies this was Chinese, not Arabic. Source inspection found that font-scale nomination still requires either multiple components or a connected glyph at least twice as wide as tall; final CJK eligibility separately permits compact single glyphs. This is a candidate cause, not a confirmed diagnosis of the user's frame. Recent runtime logs include no-centered-text-line rejections but do not identify caption text. Requested a paused failing short Chinese source frame for exact reproduction. No additional detector change or deployment was made for this report; short CJK captions remain open acceptance work.


2026-10-11 short Chinese and paused Scope follow-up: user supplied the three-character Chinese caption “叶哲泰!” and observed that Arabic mode detects it while CJK does not. Exact native V210 replay reproduced the difference (3840x2160, stride 10240, independently observed physical bounds 262-1898). The original capture sidecar had zero bounds after a cold restart; it was retained, and a separate diagnostic copy used the known bounds. This supersedes the unconfirmed font-scale nomination hypothesis for this frame. The Chinese character's disconnected upper radicals were assigned a separate row by the one-third-height baseline limit. Arabic's half-height rule joined them correctly. Applied that bounded rule to CJK in both component grouping and completed-row reconciliation, retaining overlap, union-height, spacing and backing checks. CJK native replay now detects one complete row at 1714,1866-2066,1982 (glyph 1720,1873-2056,1972). The previous native Arabic capture still produces the same complete 1638,1882-2210,1994 box. Language hints remain; this is shared geometry behavior, not OCR. Broader one-character coverage remains unqualified.

The user confirmed Scope adds side bars while paused. Logs showed the actual picture at 262-1898 but no accepted bar authority, yielding a full 3840x2160 source fit into the 2.35 viewport with 126.6 pixel side margins in the 1040 pixel window. The paused caption spoiled strict bar statistics without moving the proposed boundary. Added guarded acquisition and continued reaffirmation through the existing symmetric-bar pixel proof plus a fresh detector-confirmed caption crossing the affected edge. Reaffirmation must match existing physical bars within sampling tolerance; connected picture, near-black, NLS, fixed crop and incompatible geometry remain excluded. The regular extractor and default symmetric-hypothesis behavior are unchanged. An intermediate live deployment showed acquisition alone was insufficient because presentation admission still required current authority after the caption's hold period; the final change re-proves both aligned-rejected and caption-expanded edge observations.

Final x64 Release solution build completed successfully. Deployed both host and renderer from that build, verified SHA-256 matches, and reopened VP with native capture disabled. No config or profile state edits were made; CJK Reconstruction and Scope remain selected. Backup: `C:\Videoprocessor\vp\deployment-backups\short-cjk-scope-20261011-115228`. Configuration SHA-256: `CADDB0EA12678EF88EFD66EF39F046451A9FEC66E0DA367CCC85C91B57188E71`.

- `VideoProcessor.exe`: `8863A05C5EFADC0B7916AB6696206687052D0EB192003A1543211B1429CE3C27`

- `vprenderer\VideoProcessorVPRenderer.dll`: `8D33EB0CB54DCED9C481B993B056871E003F5784871045D1B285264AA89B6862`

No automated tests were added or run for this revision. Native reproduction and live runtime log checks are diagnostics; the earlier 407-test result does not qualify these later changes. Story remains in progress and source changes remain uncommitted; no beta merge performed.

Final runtime observations: the source had moved to a longer caption before the final deployment, so this is not a same-frame paused visual acceptance result. The current-caption certificate is observed in logs, and sequence 275 at 11:52:51 reports source presentation 0,262-3840,1896 and picture 0.0,71.2-1040.0,513.8, filling the Scope viewport horizontally. Subsequent moving frames include full-raster authority and detector instability; broad playback remains unqualified. Requested that the user pause again on the original short caption or a Scope failure for exact final validation. Do not mark the Scope or short-caption acceptance complete from these partial observations.


2026-10-11 Thai sparse edge follow-up: user is trying Thai in Arabic mode. In the first screenshot almost the whole sentence is on an opaque card above the physical picture edge; only a small lower mark crosses into the bar. User authorized trying a complete-row detection rule and then supplied a paused two-line example. The original first-screenshot replay reported no scale and backing-card rejection: only bar-entering components could nominate font scale, so the small detached mark failed to establish the letter height, and upper/lower marks remained separate.

Added a script-independent fallback only when ordinary bar-based scale nomination fails: a centered row with at least three locally backed core components, width at least three letter heights, black exterior support in all three horizontal tiles, and a nearby real bar-ink mark inside the original row's horizontal span may establish scale. Proximity counters alone cannot authorize it. That evidenced row allows one letter-height vertical mark attachment and wider compact marks, preserving association with the original core. A small already-owned physical edge mark can then anchor a plausible row only after full opaque-card proof. Existing strong bar-stroke behavior remains primary.

First-screenshot replay now detects one complete row, glyph bounds 924,1787-2904,1936 in the 3840x2160 replay raster. The earlier exact Arabic and short Chinese captures retain their previous complete bounds. The supplied two-line screenshot includes already-modified output pixels, so native input is required for its final diagnosis. The first native attempt captured startup black; diagnostic capture now skips globally near-black startup frames. No new tests or automated suite runs have been performed.

Exact native capture subsequently succeeded (3840x2160 V210, stride 10240). Original sidecar zero bounds is preserved; diagnostic copy uses independently observed 274-1884 physical bounds. The live pre-change result was only 2570,1878-2610,1922. The first revision found two rows but omitted the tall leading lower-row character (native ink starts at 1178, detected row started 1232). Extended the substantial-component height ceiling from 1.5 to 2 learned body heights only for the strongly backed sparse-edge fallback. Its larger mark corridor also required choosing the nearest original row vertically, preventing an upper-row descender from attaching to the lower row. Final native replay: overall 1170,1594-2674,1922; lower glyph 1178,1783-2664,1912; upper glyph 1253,1604-2588,1748. Inspection of native luma confirms the outer glyphs and marks are covered by the extraction padding. The original one-line screenshot and prior Arabic/CJK native captures still succeed.

The first incremental solution build hit the recurring LNK1103 corrupt incremental debug-information error. A complete x64 Release rebuild succeeded before the diagnostic deployment at 14:49:08, which is backed up at C:\Videoprocessor\vp\deployment-backups\thai-sparse-edge-20261011-144908. That intermediate live revision produced two rows but retained the leading-character omission; a final revision is being built and will replace it before completion.

Final x64 Release solution build succeeded and the complete runtime pair was deployed with hashes verified. VP reopened with diagnostic capture disabled. Arabic mode and all active configuration values were preserved; no configuration edits were made. Backup: `C:\Videoprocessor\vp\deployment-backups\thai-sparse-edge-20261011-145331`. Active configuration SHA-256: `49A1A7188F78E628772E97E0699083E499151C50A09245395E6EEFE891998306`.

- `VideoProcessor.exe`: `E488643596D25484D8F2B51E41C8341B9610B993B5B64112C4315E77FA680E3B`

- `vprenderer\VideoProcessorVPRenderer.dll`: `902E4864E60110933A53CF4ABF7A39A279758EE2082436F26BF1A9C98E38ACF3`

Final live confirmation at 14:53:51 and 14:53:56: frames 240 and 360 retained cue 5 with 71 and 191 observations, two rows, and overall bounds 1170,1594-2674,1922. Lower extraction 1173,1780-2666,1912 and upper extraction 1248,1601-2590,1748 include the native visible ink bounds. Source-card cleanup was accepted at 1110,1570-2734,1884. Startup had temporary invalid-source resets; no claim is made that all playback flashing is resolved. User visual review and broader Thai qualification remain open. Story remains in progress, with source changes uncommitted and no beta merge.
