Independent review: 2026-10-09 21-10-25.mp4 (2560x1440, 60 fps, 78.3667 s)
No production files modified. No newer recording used. There is no verified Off playback comparison; Off unaffected is the user's report.

VISUAL TIMELINE
First Beeping sequence:
14.00 s absent; 14.25 through16.00 s original black-backed [Beeping] remains unprocessed over starfield/R2.
16.25 s after cut to bright cockpit the same [Beeping] is reconstructed/moved.
16.50 s replaced by Look! There they are.
Filename-start wallclock estimate is21:10:39.25..41.00 for the miss and21:10:41.25 for acquisition; this is an estimate, not embedded frame timestamp.
beeping-timeline.jpg is a 250ms contact sheet.

Autopilot AR failure:
52.50 s scene/caption full-width. Frame3173 (52.8833 s) remains full-width.
Frame3174 (52.9000 s) abruptly changes to inner picture x312..2247 (1936px) with black borders on all four sides.
Controls are absent at transition and53.00 s, appearing only about53.50 s. Caption remains intact.
Controls fade around55.50 s but narrow presentation persists.
Thus the later AR failure cannot be attributed solely to controls; the earlier47.50 s shrink overlaps controls and is less clean evidence.

APPROXIMATE SOURCE PROBES
replay.cpp uses real current SubtitleBoxDetector and SubtitleBarEvidence/Recovery, Release x64 standalone executable.
replay.vcxproj links a scratch copy of deployed source worktree's lib; detector.cpp is copied production source. No scratch shadow production headers.
Input is rendered/resampled/encoded desktop video, NOT native capture pixels; input geometry reconstruction is approximate.
Frames15/36 full-size mapped from picture x22..2538,y174..1230 to native x0..3840,y276..1884.
Frames50/53 narrow mapped from x312..2248,y314..1126 to same native picture bounds.
Both 0px and1px vertical phase tried; detector called with 276..1884 and277..1883 bar phases.
All16 combinations detect exactlyone line without work limit. Beeping bound1666,1814..2174,1930; autopilot bound1018,1814..2818,1930 atphase0.
At frames50/53 initial ExtractSubtitleBarEvidence is unavailable (0..2160) and RecoverSubtitleInspectionBars returns invalid, yet independently revalidating known276..1884 succeeds atphase0 on both sides.
Frame53 known-edge fractions top.9996,bottom.9837; longest intrusions1,6.
Hence glyph recognition is viable on these frames when given the correct bar authority. This does not prove exact native evidence or active-picture policy behavior, but reproduces the independent extraction versus remembered-edge validation conflict.
Details in probe-results.txt; normalized probe-*.png/.bgra are preserved.

Reusable run:
bin/IndependentSubtitleReplay.exe probe-53-0.bgra 3840 2160 276 1884

REPEATABLE BASELINE/CANDIDATE CORPUS
Run rebuild_and_run.ps1 to rebuild both frozen helper variants then regenerate comparison/assertions.
Frozen production helper copies are recovery-baseline.h and recovery-candidate.h. Baseline restores exactly the original no-fallback gate. Source Detector is unchanged between versions. The executable analyzes using the EXACT RETURNED recovered bars, or reports gated=not-analyzed if authority is absent. Known-bar detector output remains an independent diagnostic, never a stand-in for successful recovery.
run_corpus.py retains frozen video frame PNGs, explicit affine source geometry, phase0/1 BGRA, SHA256 of frozen frames, all outputs and timings. Do not regenerate from any newer accidental recording.
No Off segment is inferred.

Candidate result: all8 exact-plane caption variants recover and detect1line (4frames x2pixelphases). Baseline previously failed Beeping50 andautopilot53.
Padded remembered planes (2px extra black on each side) remain an explicit limitation: the4 Beeping50/autopilot53 variants fail strict fallback. Do not claim those passed or silently substitute detector-supplied knownbars.
No-caption stars at13.5s and24s reject as no-backed-anchor even when explicitly supplied knownbars. Additional14.0s no-caption R2 frame is included to test a clearer known-edge negative.
corpus-summary.json records assertions and CPU timing. 5warmup +25measured calls per fixture, repeated twice with detectoredgephase loops. Summary uses first loop perfixture to avoiddoubleweighting.
Initial paired caption-fallback benchmark: baseline medianofmedians.461ms; candidate.716ms (+.255ms), candidatewarmmedians.703..726ms, worstfixturep95.925ms. Extra reference validation20,992samples at4K. CPU RGB screenshot-replay only; does not measure nativeP010/GPU or realplayer endtoend latency. Subsequent run timing mayvary.
