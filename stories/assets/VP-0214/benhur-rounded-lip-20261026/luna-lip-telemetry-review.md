# Ben-Hur lip follow-up: log evidence and plan review

Source: frozen `vp.log` snapshot in this diagnostics folder. Read-only review; no VP source, tests, build, or runtime changes made here.

## Observed events

- Float mode, cue 14, 19:05:50: frames 13200 and 13201 revalidated and applied cleanup `1466,1746–2375,1776`; frame 13202 failed `lip-outline-body-changed` (`applied=0`, `pixels_inspected=1`, `context_mask=0`); frame 13203 freshly reacquired and applied `1466,1746–2374,1776`; frames 13204–13209 revalidated/applied. Cue and physical/composed picture bounds stayed the same.
- Extend mode, cue 28, 19:09:46–47: frame 5378 freshly proved/applied cleanup `1232,1746–2607,1776`; then frames 5389/5390/5391/5392/5393/5394 alternate fail/success/fail/success/fail/success. Failures 5389, 5391, 5393 each report `lip-outline-body-changed`, `applied=0`, `pixels_inspected=1`, `context_mask=0`; intervening successes freshly prove/apply rounded backing cleanup with x bounds 1233–2607/2608/2609. Cue 28 and physical/composed bounds remain 384–1776. These are three dropped proof frames among six consecutive source frames, not a cue or geometry transition.
- The mode change itself occurs at 19:09:18, frame 4705; layout resets because preview is unavailable. Cue 21 is detected again at frame 4710 and its new finite rounded lip is applied, cleanup `1294,1746–2546,1776`. Thus the changeover has a short reacquisition gap, while the later cue-28 alternation also occurs under Float and Extend behavior is not the sole trigger.
- Cue 31 begins at 19:09:55 as a stable one-line cue. Its layout acquisition has `accepted_cleanup=0`; all 54 logged lip events through 19:11:42 reject `lip-no-attached-black-backing` with `pixels_inspected=1`, `context_mask=0`, `edge_mask=0x3`, and zero cleanup. This is a pixel/contour attachment rejection, not missing context or geometry authority. It may be a legitimate one-line cue without attached backing; logs cannot settle that visual question.

The log records cleanup decisions, not final rendered pixel values. In particular, `VP submitted geometry cleaned=` is temporal clean-presentation state and is not lip-cleanup evidence.

## Review of proposed row-mask approach

The separation is sound if the strict finite contour/current-cue proof continues to gate all cleanup, while the erase mask is built only from pixels independently sampled in the current frame and proven dark and neutral. Keep per-row intervals so a bright/chromatic edge collar excludes only that row’s edge pixels; do not turn one such pixel into whole-panel suppression. Keep the legacy common rectangle for the existing AR/source contract, but never treat that bounding rectangle or donor-exclusion rectangle as an erase mask. The shader must consume the exact per-row intervals in the same source coordinate space used by the proof.

Tests should exercise real composition in both Float and Extend: prove black shoulders are removed, preserve bright/chromatic rounded corners and allowed collar pixels, preserve glyphs and all pixels outside the per-row mask, and repeat across a cue replacement. Include one-pixel and multi-pixel edge variation plus missing/stale/held evidence; an old contour or held cue must never reuse its prior erase rows. Assert that retained common-rectangle/AR inputs are unchanged.

The current failure reason is too coarse to tell whether a rejected old outline differs only in its expected bright/chromatic collar or at an interior body pixel. Add minimal mismatch diagnostics before interpreting this as an edge-only issue: candidate-vs-current row and relative x, mismatch class (luma/chroma/neutral), distance from row edge, total mismatching sampled pixels/rows, and whether the point lies inside the proposed erase interval. Fail samples are about 41.2–42.6k while successful fresh scans are about 44.0k, so the existing count alone does not locate the mismatch. If it is interior, row-collar trimming is not a sufficient explanation or fix.
