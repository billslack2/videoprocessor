Jar Jar Classic rendered-video approximation, 2026-10-09

Input: C:/Users/bslac/Videos/2026-10-09 20-04-02.mp4
Desktop recording: 2560x1440, 60 fps. Samples 14.5 through 19.0 seconds at 24 Hz.
Crop viewport x544:2544, y196:1320. Estimate physical picture top/bottom from side strips,
then affine-rescale to supplied 3840x2160 source bounds y276:1884. This is an inverse
approximation of already rendered/scaled Classic output, NOT a native capture replay.
Recording compression, scaling and source cadence cannot be undone. One screenshot
boundary is imposed and screen overlay is outside the selected viewport.

Harness executes production SubtitleBoxDetector, Resolve with four frames, and
SubtitleBoxPresentation. Shared bar authority is supplied (AR policy not under test).
Detector reset per measured frame matches the shared-authority Measure path.
Baseline CSV reports the detector's same-frame raw/owned mask and presentation outcome.

Baseline: detector finds three Jar Jar lines on all samples through sequence76.
At seq52 (16.625s), presentation falls back to grace-black-panel-partial-ink.
At seq53 (16.667s), it reacquires ONLY the bottom line, new cue2, despite the current
detector still finding all three lines. Seq55 (16.750s) returns to three lines.
Upper detected/owned ink remains969 pixels throughout53-55. This reproduces loss
AFTER detection in the approximation. At seq77 (17.667s), the actual caption changes
to two lines (Do you think / the queen's idea will work?) and reacquires normally.

Do not report this as exact reproduction of native log frame identities or GPU cost.

Candidate RefreshReferencePixels replay (refreshed.csv): same three-to-one-to-three
presentation failure remains in this approximation. Instrumenting only scratch header
identifies component-coverage rejection on seq52/53, line1: native box
2052,1676--2064,1684, owned5/covered4 (80% < 85% required). Whole-line coverage was
98--99% and difference0--1%. This tiny acquired component loses one sampled pixel
while detector still reports all three lines. This is distinct from missing raw rows
and may reflect recording compression/resampling rather than native-source behavior.
component-failure.txt records exact diagnostics. Production header was not edited.

Second candidate (tiny-candidate.csv): proposed backed 5/6-pixel fringe allowance
corrects seq52 (strict match). Seq53 still legitimately reaches a larger-component
veto: native box1288,1680--1320,1752, owned59/covered50 (84.75% <85%).
The original tiny component was allowed once. Do not widen the tiny-component rule
for this different failure; source is rendered and resampled. Genuine captionchange
still works atseq77. Remaining presentationdrop seq53--54 is not claimed fixed.

Resolve pruning diagnostics (resolve-pairs.txt):
seq1 top row has1/3 support; fixedcoverage99.795%, but xbounds and normalizedsignature
vary acrossfutureframes. seq53 middle row right2552->2548 (4pixels) exceeds
SameCrowdedLine's2pixel horizontal tolerance; signature+fixedpixels pass.
Top row geometry+signature also vary while fixedpixels pass. Each has2/3 support,
so only bottom anchor is retained for reacquisition. No acquisition rules changed.

Grace rejection trace atseq53 (grace-failure.txt): contexteligible1, strict0,
confirmedReplacement1, ContainsCurrentInk1, BackedPartialInkAtReference1,
backedPartialEligible0, weak0, NoNewInkOutsideReference1, elapsed0ms,
exhausted0, expansionvisible1, currentdetectedlines3, resolvedlines1,
currentLinesConfirmed1, matchingFrames2. Thus the largercomponentveto is not the
reason boundedpartial retention cannot work: confirmedReplacement incorrectly
interprets confirmation of a pruned subset as confirmation of the full unpruned
three-line replacement. Recommended require full resolved-line confirmation.

Final semantic candidate (final-candidate.csv/final-report.json): seq53--54 retain
all3 lines andcue1 via boundedbackedpartialgrace. No presentationline losses occur
fromseq6 (fullacquisition) throughseq76. Seq55 reacquiresall3 ascue2; no claim of
unchangedcueidentity. Genuine new2-line caption isaccepted atseq77. Initial
conservative2-line acquisition seq1--5 remains unchanged. This replay does not
render reconstruction shaders or certify exact destination backing geometry.
Refresh-onlyCPU timing108calls: mean0.551ms,median0.547ms,p95 0.775ms,max1.009ms;
mean15536/max16997samples, cap65536. CPUreleaseRGBapproximation, notliveGPUcost.

Final rerun includes AcquisitionReference normalization. See final-report.json for current source hashes/timing. Prior numeric timings above describe previous run. Presentation changes: [{'sequence': 1, 'cue': 1, 'lines': 2, 'reason': 'detected'}, {'sequence': 6, 'cue': 1, 'lines': 3, 'reason': 'detected'}, {'sequence': 55, 'cue': 2, 'lines': 3, 'reason': 'detected'}, {'sequence': 77, 'cue': 3, 'lines': 2, 'reason': 'detected'}]

FINAL candidate (see final-report.json and final-candidate.csv):
Independent continuity evidence plus full-replacement confirmation semantics and
complete capture-mask retention eliminate presentation line loss at53-54. No
line loss occurs after full acquisition at6 through76. Reacquisition of allthree
occurs at55; genuine nextcaption at77 is accepted. Initial conservative2-line
onset remains. Acquisition reference is restricted to accepted current-owned rows.
All results remain a rendered-video approximation, not a native capture replay.
