# VP-0070 Phantom Menace recording regression — db757d0d

Status: **Blocked**, unchanged. This is a detection-only experimental trial,
not acceptance of extraction, relocation, or general live-input reliability.

Source commit: `db757d0de6734efc66aec99f0f8f0a5dd0da7b25`, branch
`codex/subtitle-phantom`, worktree `E:\codex\videoprocessor\subtitle-phantom`.
The GitHub beta/default branch was discovered as `v1.3.005-beta` at
`c84863eaa59e03fe218de824c34b7c0220c4fd87`; the previous subtitle experiment
was carried onto that fresh baseline. Existing branches/worktrees are preserved.
The feature commit is saved locally. Automatic approval review rejected public
GitHub upload of the feature branch; explicit user approval is still required.

## Changes and review

Dedicated current-pixel bar proof replaces crop-derived authority for this
diagnostic only. Independent edges, local boundary contrast, flat-band support
and contiguous-intrusion rejection handle crossing glyphs without changing crop
authority. One source row of bar-measurement variation is tolerated only with
fresh proof; missing bars and larger moves invalidate the cue.

The detector samples boundary rows explicitly, accepts a single bar sample
belonging to a substantial letter stroke, derives scale from centered groups,
reconciles fragments of the same baseline, preserves word spacing and detached
question marks, and rejects mismatched picture-side text cores. Small native
neighborhoods recover thin strokes and terminal dots missed by the coarse grid.
Antialias fringes retain the looser mask threshold. Signatures include owned
line components only; established cue bounds remain fixed. No OCR or pixel moving.

Three agents reviewed the real clip, bar/queue integration, adversarial cases,
source ownership/indexing and replay acceptance. No remaining blocking code
review findings. Mutation checks demonstrate that disabling tiny-dot, thin-core
and core-color fixes fails their respective new tests.

## Pinned build and tests

- Clean x64 Release solution rebuild succeeded (0 errors, 45 existing dependency warnings).
- Native suite: **1,966/1,966 passed**, including **81 subtitle tests**.
- Config suite: **82 PASS checks**, exit 0; offscreen/synthetic display coverage.
- Physical two-monitor Config placement was unavailable on this one-monitor host.
- Exact staged configuration passes the production reader; diagnostic enabled.
- Isolated executable `/help` smoke exits 0. Playback was not launched.

MSBuild: Visual Studio 18 Professional; application toolset 14.29.30133,
Config toolset 14.44.35207. Official licensed VS app-local Release runtimes
were staged with architecture/signature/version/dependency validation.

Host SHA-256: `4EB2EABDDE7645843BE5381860EE60A3C561FA608DB0997E9C1DC6DE4A598EDA`.
Renderer SHA-256: `28909CF2E4AEA30FF9E539145C8CD8847296040D76C82D4DA3BD978A996F337D`.
Final replay probe SHA-256:
`cd0ec4fbc988e9d073b9a5a215d3ed6857425142d43e195d26d086dd54ea25ed`.
Build runtime records and all payload hashes are retained in `test-copy.json`.

## Actual recording replay

Original private local recording: `C:\Users\bslac\Videos\2026-10-02 21-25-45.mp4`.
SHA-256: `4c5b3da9c702141dd9ad5223ffa094eb9c6453a28e41dd70863c13e1fe03c9ea`.
Decoded 2560x1440/60fps desktop recording, 4,688 frames. Native movie crops are
1040x585 and 2000x1125; no resize, OCR, old-overlay removal or image reconstruction.
The crop phases start separate processes; window maximization is not an AR cut.

Frozen manual annotations comprise **16 interior positive cue intervals and
3 independently annotated complete cues**, plus negative and contaminated
intervals. These labels informed iteration and are not an unseen validation set.
Unannotated and original-overlay frames flow through replay but do not count
toward clean acceptance. All original/derived media remain local, outside Git.

| Pinned production-pipeline replay | Positive frames complete | Negative boxes | Stable intervals |
|---|---:|---:|---:|
| 24000/1001 sampling, 3 future frames | 807/807 | 0/234 | 19/19 |
| Every 60fps captured frame, 3 future frames | 2,018/2,018 | 0/586 | 19/19 |

Strict containment, correct line count, bounded excess margins and zero box
coordinate drift all pass. The complete cues begin at capture frames 2113,
3299 and 4288; each has a full box on its first captured frame. Other intervals
make no onset claim. Repeated desktop frames are not independent input frames.

Queue depths 0, 1, 3 and 7 all pass the sampled regression. These variants used
the reviewed candidate10 probe; every non-timing output field matches the clean
Release-linked probe across all 1,787 sampled and 4,471 full-capture replay frames.
The old pipeline displayed 0/807 clean positive samples because native crop
authority withheld the detector. Acceptance compares actual recorded pixels,
not synthetic subtitles or supplied bar boundaries.

Final sampled bar-proof/text/resolve CPU time: windowed mean 2.373 ms, p95 2.552 ms;
maximized mean 3.210 ms, p95 3.595 ms. These shared-host offline measurements
exclude decode/UI/render and do not establish original-4K capture performance.

## Broader corpus and remaining limitations

The unchanged HD/UHD synthetic-on-real-background corpus supplies known bar
bounds and tests spatial detection only. Complete positive bounds improved
168→186/198 at HD and 177→183/198 at UHD; negatives remain 0/360 at each size.
Its continuous-cue geometry still changes (2 HD and 3 UHD distinct boxes,
previously 3 and 4). The strict area-ratio<=1.5 score is 102/198 HD and 153/198
UHD, versus 168/198 and 156/198 before; conservative padding around short words
causes many HD area failures. These limits are retained, not relabelled as passes.

This recording regression does not qualify all fonts, backgrounds, HDR inputs,
source AR transitions or original-4K live capture. The bar/queue transition
behaviors have unit coverage; wider live acceptance remains outstanding.

## Runnable isolated copy

`E:\codex\subtitle-phantom-test-db757d0d\START-SUBTITLE-TEST.cmd`

Close an existing VP instance before launching. Choose the physical 16:9 screen
profile. The diagnostic fits the full input raster and draws a green three-pixel
outline; it does not move text. Config and state were copied byte-for-byte from
the previous isolated trial and backed up under `backups/`:

- `VideoProcessor.cfg.before-subtitle-phantom-20261003-014100`
- `VideoProcessor.state.before-subtitle-phantom-20261003-014100`

No configuration edits. The usual C: installation was untouched. The paired
host/renderer hashes match the same pinned Release build. No system runtime
installer, desktop shortcut, merge or release publishing was performed.

`REPLAY-RECORDED-CLIP.cmd` reruns the actual local recording test and creates a
new report/preview directory. It uses the existing Python/OpenCV installation
and optionally the existing ffmpeg encoder. Playable H.264 previews are under
`replay/windowed-preview.mp4` and `replay/maximized-preview.mp4`.

Build logs, TRX, frame maps, per-frame CSV and all iteration reports are retained
under the source worktree's `artifacts/`. The numerical reports and copy receipt
are attached here; the video and decoded frames are not published.
