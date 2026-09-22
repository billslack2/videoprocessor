# Available-frame lookahead and inward confirmation

Base: beta `6785d0b1092b445a8b05136c2bce1249a09e7d4f`.

## Change

Queue preview now uses a tested selector that counts actual pending future
source frames separately from its inspection limit. Cadence repeats never
contribute evidence. Existing integer configuration remains a hard maximum:
`effective = min(configured, available, 8)`; zero remains disabled. Setting 8
uses the available queue up to the existing analysis budget. Selection does not
consume frames, change queue targets, add waiting, or change capture latency.
The previous selected prefix is preserved; the old availability diagnostic was
truncated to the configured limit.

For an established trusted crop, live retention already inspects every source
frame. General preview was still using the sparse acquisition cadence, allowing
live confirmation to overtake it. A new exact inward certificate uses a copy of
the current live transition model at the established-crop cadence. Candidate
evidence is reset in the copy: no pre-cut/pre-window votes may shorten proof.
Stable geometry, remembered formats, thresholds and deadbands are preserved.
The live model never receives future frame counters.

The complete certificate must start on the current queued frame, contain an
exact same-axis inward target on every contributing frame, and have trusted,
non-near-black evidence. All identities must remain pending in the same
transport, source-format, viewport, renderer, continuity and policy context.
At consumption, current pixel bounds/classification and the live stable
reference must still match. Subtitle ownership, translation and moving-format
states veto the new path. A mismatch retains existing live processing.

## Scope limits from independent review

Existing three-frame broad outward proof remains unchanged. Near-black
acquisition, recovery dwell, subtitle FIT/translation and moving-format settling
remain live policies. Their evidence includes presentation history that is not
fully available in the preview; accelerating those timers without equivalent
certificates would weaken safeguards. Full-raster/startup acquisition is also
outside the new inward path. No detection threshold, aspect deadband, display
profile, deployed configuration or deployed runtime has been changed by this work.

## Validation

`artifacts/lookahead/red.trx` records four expected failures with the original
queue-counting logic and sparse preview cadence extracted into the production
helpers: three availability cases and adjacent inward proof timing. This is a
logic-level reproduction, not a recorded end-to-end renderer failure.
`green-targeted.trx` records the corrected cases plus independent adversarial
checks passing. Further native-suite and queue-to-presentation replay results
are recorded alongside them before handoff.

Independent implementation review covered candidate reset at scene cuts,
identity/continuity, native-versus-converted measurement mismatch, final live
admission and immutable live counters. Independent QA covered actual queue
selection, short/disabled windows, repeats, depth-to-display timing and return
transition order. Real HDMI playback/performance remains to be validated after
an explicitly requested local deployment.

Final results: x64 Release solution build succeeded. All 1,410 native tests
passed (`artifacts/lookahead/full-native.trx`), including 15 added tests and
all existing crop/near-black/subtitle/moving-format regressions. The queue
selection matrix covers configured depths 0/1/2/3/5/8 and actual future counts
0/1/2/8, with cadence repeats. The inward P010 pixel replay confirms final
scope presentation on source frame 100 with sufficient buffered proof versus
frame 101 with lookahead disabled. A third fresh review found no blocking
issues. No local deployment or merge was performed.

One incremental test link hit MSVC LNK1103 (corrupt debugging information).
A clean rebuild of the test project resolved it; the completed full-suite
result above is from that rebuilt test binary. This was a build artifact
failure, not a test or production-code failure.
