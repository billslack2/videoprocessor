# Near-black bounded recovery — 2026-09-28

## Evidence and regression attribution

Baseline is remote v1.3.005-beta at 765db7b89ab7a110f6437ac089458374dec6d537. Running host/renderer reported 87ebf4c69a923bbf7e98935fb474bc4e6a8220cb. The prior acquisition/refinement and fit-envelope fix remains present; its detector/policy/renderer files were unchanged by the later config-selection change. Current user configuration was not overwritten or edited for this investigation.

The repeated TS5 event at 15:22:33 (source 30120–30127), matching the 15:10 replays, retained a native crop (0,42)-(3840,2118). A near-black episode was already retaining that crop. A bright subsequent frame found top-band pixels at rows 40/41 (first Y/U/V=99/512/515, peak Y=103, cutoff=96). The pixel-safety evaluator supplied a bounded outward rectangle (0,27)-(3840,2118), including its conservative margin. The episode instead showed full raster for seven frames / 297 ms, then returned to the original crop.

The unconditional RETAIN_CROP → FULL_RASTER branch predates the recent fixes (git blame: 4f61b3405, August 29). The earlier passing tests covered paused/startup acquisition and small ordinary fit-envelope changes, not this retained-crop near-black episode. This is evidence of a coverage gap in an existing policy path; it does not prove that every historical visual report had the same cause.

## Correction

Keep episode ownership, logical crop, learning, thresholds, and the existing seven-frame proof unchanged. After ordinary recovery and before crop admission, permit a temporary outward presentation only around the already-admitted native entry crop, using the existing current-frame bounded-visible-pixel certificate. It must match source generation, sequence, presentation epoch and base crop, contain the raw current observation, and pass all competing-owner/context guards. Round only outward for chroma. Hold the union until recovery completes, exposing new outward growth immediately. Loss of proof after admission conservatively latches full raster for the remaining episode. Startup and sparse/learned entry crops are excluded from this new route.

Optional aspect-limit fill is explicitly withheld for this envelope, including when NLS is disabled. Operator-requested fixed-aspect crop retains its existing intentional priority. No extra pixel scan or downscale is introduced. Standard change-triggered diagnostics include renderer instance, source identity, bounds and reason.

## Test-first validation

- Initial x64 Release test run: two positive replay tests failed against the unchanged full-raster presentation behavior exposed through a no-op seam; two negative tests passed. See artifacts/validation/red/tests.trx.
- Initial correction: all seven then-current regression tests passed, including real synthetic P010 and native v210 pixel-buffer evidence, growth/retention, repeats, stale evidence and the actual unchanged seven-frame episode proof.
- Independent review found the real renderer demotes native BAR_CROP_TRUSTED to PROVISIONAL during the episode. A new test using ConstrainNearBlackCropAcquisition reproduced that integration failure before correction; see artifacts/validation/classification-red/tests.trx. The fix accepts only that existing policy transformation alongside the raw retention classification.
- Final tests additionally cover genuine raster expansion and 37 invalid/stale/competing-context variations. Existing tests/expectations were not weakened.
- Final clean x64 Release rebuild succeeded; 1,858/1,858 tests passed (including nine new tests). See artifacts/validation/final/tests.trx. Independent final review found no remaining blockers. An earlier incremental build hit MSVC LNK1103; the clean rebuild resolved it. No existing test assertions were changed.

## Scope and remaining validation

Tests replay measured geometry/state and synthetic source pixels; they are not a recording of the user's exact HDMI sequence. The expected remaining presentation adjustment is the small conservative outward envelope, rather than full raster; do not claim zero visible change. Replay the same TS5 passage to check the new diagnostic and final-layout records before claiming live resolution. The validated pair was deployed at 15:50:57 EDT to C:\Videoprocessor\vp, with both hashes verified and configuration byte-identical. Backup: C:\Videoprocessor\vp\deployment-backups\near-black-bounded-20260928-155057. The user replayed the problematic scene and reported: "looks perfect to me." This confirms that replay, not every possible source sequence.


