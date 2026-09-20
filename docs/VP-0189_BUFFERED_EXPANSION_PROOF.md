# VP-0189: confirm picture expansion before dequeue

## Measured problem

The 40ad343e replay at 23:11:48-23:13:03 on September 19 contains seven
scope-to-taller transitions. Six retain the old crop for three source intervals
(about 125 ms at 24 fps), and one for two intervals (about 83 ms). They apply
one final crop without the previous intermediate subtitle FIT excursion.

The configured and available lookahead are both two future frames. The queued
model can already confirm an outward decision, but the live expansion gate
rejects it while accumulating its own three observations. A second sequencing
problem removes/marks the displayed frame consumed before analyzing its future
neighbors, making a three-observation proof too late for that first frame.

## Correction

- Preview the current frame plus the configured future source frames before
  dequeue. Use only the available queue; do not wait for additional proof or
  change queue depth, prefill targets, or capture cadence.
- Cache existing raw preview measurements per queued frame. For a possible
  full-width, both-edge vertical expansion, measure the current and next two
  source frames against one frozen live crop using the existing pixel inspector.
- Reuse the existing anchored one-detector-step confirmation: four pixels at 4K,
  two at 1080p. Require trusted bar geometry, matching exact expansion-strip
  evidence, and affirmative non-near-black evidence on all three frames.
- Bind the proof to the first frame's exact bounds and identity. Future bounds
  do not replace the displayed frame's coordinates. Do not seed live temporal
  state with future counters.
- Require all contributing identities to remain queued and belong to the same
  current timeline continuity segment. Accepted sequence and supplied capture
  counters must be consecutive; context, policy and current-frame identity are
  checked again before use. A fresh generation stamp alone is not proof.
- The live consumer still validates its actual current pixels, exact old base,
  current target, composition veto, model deadbands and publication admission.
  Existing subtitle FIT/translation/drift ownership excludes this fast path.
  Skip extra scans when that exclusion is already known at preview time.
- Normal post-publication retention, recovery and final presentation admission
  remain active. Insufficient lookahead or ambiguous evidence uses the existing
  conservative live path. Full-raster, asymmetric and mixed-axis changes are
  outside this narrow optimization.

The native preview sampler supports the same native RGB/YUV capture formats as
before, independently of the subsequent renderer conversion. Live target/pixel
validation remains mandatory; differing native/converted measurements fall back
rather than relaxing exact current-target matching.

## Evidence and review

Working evidence: C:\Users\bslac\AppData\Local\Temp\vp-lookahead-proof.
The preserved pre-fix live log contains the measured transition timings above.

`red-lookahead.trx`: unchanged production, two tests. The physical three-frame
expansion proof passed; first-frame publication failed at the live gate, as
predicted. This is a real P010 pixel fixture with timeline/admission integration,
not a decoded replay of the film or a GPU screen-output assertion.

`green-buffered-full.trx`: clean x64 Release solution rebuild; 1347/1347 passed.
Two independent reviews covered proof semantics, current-frame publication,
queue/source-buffer ownership, locking, stale evidence, subtitle interactions,
and small-measurement behavior. Review found and corrected the continuity-stamp
loophole and added three timeline regressions.

`final-buffered-fixed.trx`: clean x64 Release solution rebuild; **1350/1350
passed**. All twelve changed/new source files match the pre-build snapshot.
Eleven new proof/continuation tests and three timeline tests cover first-frame
adoption, insufficient lookahead, stale/context/capture gaps, subtitle vetoes,
anchored measurement noise, repeated transitions and eight following frames
with unchanged bar pixels. A separate physical 72-to-68-pixel growth control
confirms new picture pixels are not clean-bar measurement noise; it does not
establish a visible bounce. No additional production guard was justified by it.

The preceding final build attempt failed on an unqualified enum in the new
preflight; correcting its namespace required no behavior change. Its failed
build log is retained. Both final independent reviews found no concrete blocker.
Real HDMI cadence, concurrent resize/profile changes, perceptual smoothness and
extra inspection cost remain live-validation items. No deployment occurred.

Durable evidence: C:\Users\bslac\Documents\ChatGPT\Done\crop-lookahead-proof-20260919.
Validation preceded the source commit; the receipt records that identity and
snapshot comparison. Deployment requires rebuilding the clean committed identity.

## Replay qualification

After an explicitly requested deployment, verify matching clean host/renderer
identities. On the same scope-to-taller cut, the log should show a buffered proof
covering current through current+2, `current_valid=1`, and an applied lookahead
decision effective on that first changed frame with `proof_frames=3`.

`Alpha buffered picture inspection` records the extra inspection cost in ms.
`Alpha buffered picture proof` reports current validation. Preserve fallback and
rejection diagnostics when a frame does not qualify. Watch repeated transitions,
subtitles, dark introductions and source/profile changes. Keep VP-0189 in Review.
