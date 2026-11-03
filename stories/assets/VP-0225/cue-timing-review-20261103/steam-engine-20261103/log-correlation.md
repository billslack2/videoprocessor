# Steam-engine playback: source/log correlation
Date: 2026-11-03
Scope: read-only correlation of C:\Videoprocessor\vp\logs\vp.log and rendered-output capture VP-rendered-20261103-125619-061-frame-6842.json. current_gating owns recording-visual review. No runtime/source changes or GPU work were made.

## Paused anchor: source 6562 / output 6560

At local 12:56:07.358 (overlay 17:56:07.358Z), renderer instance 3CF5A543-32F3-476F-8795-C654B3B9E6B0 generation 1 selected current source 6562.

- Physical accepted crop remains [0,270,3840,1886], accepted from source 486. Current measured candidate is [0,272,3840,1888], but the reason is “native candidate has an unverified nominated crop axis”; observation_supports_crop=false and retention_safe=false. This candidate was not accepted as a new native crop.
- The presentation decision reports cue32 detected=true, held=true, incomplete=false, bounds [1518,1926,2310,2022]. Absence is false. The output is not composed or source-prepared; reason is “raw subtitle fallback to current source: subtitle inspection pending”.
- Admission is ready=1, unprocessed=1, caption_state=0, composed=0, last_submitted_source=3120. Current raw visibility extends through y=2025; selected current-source crop is [0,270,3840,2026].
- That crop includes the caption’s y=2022 bottom. It is about 2.19:1, consistent with the “AR in use 2.20 held” OSD. The accepted physical crop is about 2.38:1 (displayed as 2.39). This is a presentation/fallback mismatch, not a logged physical-edge update.

## Transition into fallback

At local 12:53:43, source 3119 acquired cue32 with glyphs-match, and source 3120 was the last processed submission using source crop [22,270,3818,1886]. The accepted physical geometry remained [0,270,3840,1886].

At source 3121 the continuation record became held=true, decision=strict-current-ink. The layout record says reason=hidden, tracker=strict-current-ink, match=glyphs-match, backing=1, reference_refresh=reset. Admissions from 3121 onward say unprocessed=1, caption_state=0, composed=0, last_submitted_source=3120 and use current-source fallback. The first recorded composed-to-fallback transition follows a held strict-current-ink presentation result and a layout hide/reset, not a new physical crop.

The current-cue worker proof has a matching rejection signature, cadence-current-glyph-changed. This is evidence that the seeded glyph comparison failed, but it does not establish why the replacement template or prepared certificate was not recorded.

## Raw current scan versus accepted presentation result

The long-run subtitle-output trace reports flags=91, including bit64. The renderer’s bit64 contract says the exact current raw scan is detected, nonheld, not confirmation-pending, with independent bar analysis and complete raw ink. The accepted tracker/presentation result may separately be held=true. These are different objects/states.

Therefore the raw-held predicate in CompleteSemanticScanConfirmsCue is not established as the failure: the available trace says the raw worker observation is nonheld, so that predicate should pass if the other scan/cue/context comparisons match. SubmitCurrentCueTemplate rejects a held acceptedCue, which is a possible replacement gate if invoked with the held presentation result; the trace does not show whether the call occurred or which Record precondition failed. SubtitleCurrentPreparedCueCertificate::Matches also expects nonheld raw current text and a held accepted result, so accepted.held alone does not prove that certificate matching failed. The precise failed Record/Matches predicate remains unknown.

## Persistence

For every correlation source from 3126 through 14671 (11,546 consecutive source IDs, about eight minutes), the renderer reports the same state: source crop [0,270,3840,2026], accepted native [0,270,3840,1886] from source 486, measured [0,272,3840,1888], cue32 detected and held, composed=false, sourcePrepared=false, and subtitle-inspection-pending fallback. Across sources 6000–7050, all 1,051 rows carry that same crop and fallback state. This is persistent fallback, not a brief one- or two-second confirmation delay.

## Finding and next reproduction

At the paused anchor the physical 2.39 result is retained, but no current prepared subtitle composition is accepted. The renderer widens the current-source crop to keep the detected caption visible, yielding about 2.20 in use. A 1–2 second AR hold would not address this recorded cause. The evidence points to the subtitle path leaving presentation composition pending after a strict-current-ink result; the exact template/certificate rejection still needs isolation.

A focused CPU raw replay should seed a detector-authentic dim cue and its current comparison template, then feed a brighter source with the same text/layout. Assert the template proof reports cadence-current-glyph-changed. For the same frame, provide a complete independent current raw scan whose raw observation is detected and nonheld while the tracker presentation result is held. Separately assert CompleteSemanticScanConfirmsCue on the raw observation; record whether SubmitCurrentCueTemplate is actually invoked and whether its held acceptedCue guard rejects it; and inspect the exact CurrentPreparedCueCertificate::Record/Matches result. Add a genuinely changed glyph/new line as a negative. Keep all held/semantic/native/absence authority unchanged and use current raw pixels on any failed or pending path.

## Evidence

- C:\Videoprocessor\vp\screenshots\VP-rendered-20261103-125619-061-frame-6842.json
- C:\Videoprocessor\vp\logs\vp.log: S6562 admission/correlation (12:56:07), S3119–3122 continuation/layout/admission (12:53:43), S3126–14671 correlation rows, worker-cue-proof and subtitle-output traces.
- E:\codex\videoprocessor\combined-ar-stutter-20261102\src\VideoProcessor-Lib\SubtitleCadenceGuard.h
- E:\codex\videoprocessor\combined-ar-stutter-20261102\src\VideoProcessor-Lib\SubtitleMeasurementWorker.h
- E:\codex\videoprocessor\combined-ar-stutter-20261102\src\VideoProcessor-Lib\SubtitleCurrentPreparedCueCertificate.h

