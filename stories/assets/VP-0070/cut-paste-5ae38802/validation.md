# VP-0070: bounded dropout rescue and rectangular cut/paste trial

Experimental follow-up; story remains Blocked. Based on freshly verified beta
v1.3.005-beta, c84863eaa59e03fe218de824c34b7c0220c4fd87, with the prior subtitle
trial commits replayed into an isolated E: worktree. This is not production glyph
extraction or a claim that arbitrary subtitle material is recognized.

## Behavior

- `subtitle_bbox_test: true`: full-raster, unwarped view; green three-output-pixel
  outline expands the accepted detector rectangle by 10 source pixels per side,
  clipped at raster boundaries. Raw detection geometry/signatures stay unchanged.
- `subtitle_cut_paste_test: true`: same diagnostic view; sample the padded rectangle
  from the current frame, translate upward with its bottom 15 source pixels above
  the accepted active-picture bottom, and clear remaining old-source pixels black.
  Destination takes precedence over clearing when rectangles overlap. Picture
  pixels inside the rectangle move too. Too-tall/top-bar/non-upward placements
  leave the input unchanged. The hook operates at combined RGB before scaling
  and output color management, including HDR input.
- Stable cue geometry and anchored placement are reused across repeats and a
  one-row proved boundary fringe. Pixels always come from the current frame.
- An established cue may bridge at most two distinct source frames only when
  current fixed-coordinate ink still supports every accepted line and a matching
  cue returns within that bound in the existing queue. No added buffering or
  readback. Blank pixels, changed text, missing bar proof, discontinuity or stale
  context fail closed. A future companion line must also have current ink.

The two launchers select mutually exclusive flags in separate configs derived
from the existing isolated user's trial config; no source example replaces it.
The original cfg/state and timestamped backups are retained. Normal installation
and live playback are untouched.

## Efficiency

Queued analysis is still cached by exact frame identity. Every new frame remains
eligible for scanning so new cues and additional lines can be recognized early.
Packed downsampled raw/owned masks are retained with bounded queued observations;
fixed-coordinate comparison is used only for dropout rescue or borrowed lines.
The shader is parsed once per renderer lifetime; placement is reused for the cue.
No historical picture patch is cached, and the rendered frame never samples a
previous frame. Full 4K glyph analysis is not introduced (grid at most 960x540).

## Verification

Pre-pin validation: all 1,989 native tests and the focused temporal suite passed;
final clean pinned-build validation is recorded in the isolated test-copy receipt.
Actual recording replay retained 807/807 sampled positives and 2,018/2,018 every-
capture-frame positives, 19 stable cue intervals, and 0/234 / 0/586 negative boxes.
All positive placements met the separate padding, inset and stability gates.
Explicit simulated one/two-frame segmentation failures on unchanged recording
pixels held the same box; blank controls remained clear. Frozen Phantom Menace labels and raw tightness,
completeness, negative and stability gates are unchanged. Padding/placement checks
are separate. The recording is a desktop capture, not original 4K capture input;
its labels informed earlier iteration and are regression coverage, not unseen data.
CPU geometry tests and actual offscreen WARP RGB/P010-PQ readback tests exercise
clipping, overlap, current pixels, dynamic geometry rebinding and disabled identity.
Live capture performance and arbitrary content remain unqualified.

## Final pinned verification

Commit 5ae388020caf402079f15b627d79f82f8d56c885; clean x64 Release rebuild passed.
Native 1,990/1,990 and Config 83/83 passed. Sampled 807/807 and every-capture 2,018/2,018
positives complete with valid stable cut placement; 0/234 and 0/586 negative boxes.
Three explicitly injected positive failure frames held the same cue/rectangle;
blank controls did not hold. Real offscreen RGB and HDR P010 GPU pixel tests pass.
No live playback qualification or universal subtitle recognition claim.

Run E:\codex\subtitle-cut-paste-test-5ae38802\START-SUBTITLE-BOX.cmd or
START-SUBTITLE-MOVE.cmd. Both force a full-raster diagnostic view.
Original cfg/state preserved byte-for-byte with timestamped backups in backups/.
Separate mode cfgs change only subtitle_bbox_test and subtitle_cut_paste_test.
Both exact cfgs validated with production parser. Normal install untouched.
Source saved locally; public source upload still awaits explicit approval.
