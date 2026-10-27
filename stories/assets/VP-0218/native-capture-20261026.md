# VP-0218: paused YouTube TV ad native capture

Capture time: 2026-10-26 23:24:51 America/New_York. Analysis used unchanged `billslack2/videoprocessor` beta `v1.4.02-beta` at `7394ba420999a8fa708718ee4ce4d87e7459369f`. The running host/renderer logged source `326012fd1c2f0f038815b98d526da83897972d20`; [eight inspected AR files match](source-parity.json) the reviewed beta by Git blob identity. This is source parity, not a new binary audit.

## Conclusion

The saved example persistently fails acquisition because the upper exterior does not satisfy existing bar discovery or excluded-pixel safety, although its transition into the red picture is strong. The lower bar is trusted. All 97 native payloads replay as `PROVISIONAL`; all 1,516 recorded presentation decisions retain the full raster. Confirmations have already saturated, global darkness is false, and lookahead has no trusted base. Changing a confirmation timer cannot repair this particular failure.

The upper band contains a substantial gray gradient and fine texture; the lower band is nearly flat black. This contradicts the initial proposal that matching uniform matte references would fix the captured example. A separate independently qualified per-edge spatial profile is now specified in the [revised design](adaptive-evidence-design.md#captured-ad-independently-qualified-variable-exterior-profiles). That proposal is not implemented or validated, and a visible border alone does not prove that the outside artwork is expendable.

## CIH interpretation corrected on 2026-10-27

The user reaffirmed the intended scope/constant-image-height presentation. The active deployment configuration has a 2.35:1 Scope viewport, automatic cropping enabled, centered alignment, and aspect-fill limits 2.32 (narrower) and 2.42 (wider). The saved state selects both Scope viewport and Scope zoom, with NLS Off. These settings explain the display context, not the detector objective; asking the user to choose between the incoming 16:9 raster and the detected picture was unnecessary. The detector must measure the active picture independently of these settings.

The 3840x2160 input is a 16:9 transport container. The red picture rectangle between the measured horizontal transitions is approximately 2.47:1. All 1,516 recorded presentations retain the entire input raster. Under the configured CIH mapping, this treats the outer bands as part of a 16:9 picture: the raster fits the scope screen height and occupies only about 75.7% of its width, while the red picture is reduced further vertically by the retained bands. Full-raster retention therefore does not demonstrate successful AR detection; it explains how a scope picture can remain undersized with borders on all four sides.

The user subsequently clarified that the task is accurate, reliable black-bar and active-aspect detection without regressions, independent of any specific fitting. Screen geometry and CIH must not influence bar classification or force a target aspect. The following mapping is only a downstream consequence of a correct measurement. A qualified picture near 2.47:1 would use screen width with small vertical margins; it is beyond the current wider-fill limit of 2.42 and must not be forced to 2.35 by changing configuration. Genuine 16:9 content should continue to fit screen height with side margins. The capture has no transition onset and does not establish the timing of an earlier scope-to-16:9 failure.

The straight, distributed upper/lower boundary measurements are useful evidence even though the current upper black-level/profile predicates fail. Failure of the coarse linear exterior model does not negate boundary evidence or by itself label the exterior as scene content. Continue independent per-edge qualification and validate the final displayed picture size, exact removed pixels, subtitle retention and configured fill limits. This correction does not claim a new detector fix, a qualified hard crop, or a new deployment.

## Capture and method

Original data remains at `C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1`. Local replay/preview artifacts are at `E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay`.

- 97 native payloads, each 22,118,400 bytes: 3840x2160, stride 10240, NativeYuv422/V210, Rec.709, Limited range. Sidecars record full-height context `(top=0,bottom=2160)`; those coordinates do not establish detected bars.
- Source frame/accepted identities, domains and generations are preserved. The loader follows production replay semantics, including binding the sample source generation to the identity's transport generation.
- Unchanged production evidence, darkness and relative/retention functions were compiled into a standalone x64 optimized diagnostic with MSVC 19.29.30152, `/O2 /MD /DNDEBUG`. This is a component characterization, not a complete VP solution build or renderer replay.
- Every payload received the same production evidence measurements. Additional hypothetical-boundary and native paired-row measurements use only frames 1, 49 and 97. These sets are distinguished in the run record and CSVs.
- A separate NumPy decoder follows the production V210 lane mapping to inspect original ten-bit Y/U/V. Its Rec.709 Limited preview is an analytical rendering, not a calibrated display capture. No source pixels were edited.

## Actual recorded presentation

| Observation | Recorded result |
| --- | --- |
| AR decisions | 1,510; all `PROVISIONAL` |
| Candidate/measured rectangle | `(0,0)-(3840,1858)` |
| Model reason | `provisional geometry lacks affirmative crop authority` |
| Confirmations | Saturated at 255 |
| AR publication | No publish flags |
| Effective/displayed rectangle | All 1,516 presentation records: `(0,0)-(3840,2160)` |
| Presentation owner | Generation-current `FULL_RASTER` |
| Dark/recovery block | No near-black, recovery, episode or acquisition-deferral flags |
| Black reference | 1,505 rows: Limited `legacy`, floor 64/cutoff 88 |
| Lookahead | Configured 5, normally 4 futures available; all 1,506 rows `no-trusted-base` |
| Axes | Vertical `bar-edge-rejected`; horizontal `no-full-extent-support` |

`ar-decision.published` is populated from `transition.bounds` even when its publish flag is false (`LibplaceboVideoRenderer.cpp:11987`). Its apparent 1858 coordinate is not evidence of a published crop. The presentation rows establish the effective/displayed rectangle. The zero `ar-edges` values are unavailable retention observations, not a measurement of zero-valued black bars.

The current-layout record has auto crop enabled, fixed crop off, 2.35 screen, narrower/wider limits 2.32/2.42 and NLS off. That changed-layout record predates the paused sample but uses the same viewport epoch 8; actual presentation rows confirm full raster with no fill applied. This is not evidence that optional-fill settings caused the miss.

## Native detector replay: all 97 frames

| Measurement | Result |
| --- | --- |
| Classification | `PROVISIONAL` on all 97 |
| Proposed / trusted fallback | `(0,0)-(3840,1858)` / full `(0,0)-(3840,2160)` |
| Top evidence | No nominated band; `barPixels=0`, unmeasured |
| Bottom evidence | Trusted 302-row band; black fraction 1.0; P90 64-68; dispersion 0-4 |
| Global P90 / near-black | 268-272 / false (cutoff 96) |
| Existing relative-contrast path | `geometry-not-eligible` before sampling, using recorded full-height context |

A missing top measurement must not be interpreted as a black top band or a completed negative structure scan. Existing local boundary recovery also needs already trusted opposite bars; it cannot simply use this strong lower edge to create upper authority.

## Diagnostic measurements: first, middle and last payloads

The following interrogates a **hypothesis** `(top=302,bottom=1858)` mirrored from the trusted bottom. It never establishes or publishes that crop.

| Upper-band measurement | Measured value | Current ordinary criterion |
| --- | --- | --- |
| Top-row samples at/below cutoff 88 | 21/48 | At least 44/48 for nomination |
| Top-row samples at/below 104 | 31/48 | Still below 44/48 |
| Hypothetical excluded black fraction | 0.50347 | At least 0.95 |
| P90 | 180 | At most current cutoff 88 |
| Dispersion | 108 | At most 24 |
| Sample texture | 6.894 | At most 8 |
| Neutral chroma fraction | 1.0 | At least 0.90 |
| Inner contrast | 190.8-194.8 | At least 10 for this band size |
| Black-line continuity | 0 | At least 0.99 |

Thus chroma neutrality, existing texture limit and mean contrast pass while magnitude/uniformity/continuity fail. Raising a single black threshold is insufficient. Current retention separately enforces P90<=floor+24 and <=104, dispersion<=24 and its other pixel-safety checks. It rejects the hypothetical upper band and requests outward expansion to row 0. Existing symmetric startup recovery also remains provisional.

At the first payload's native 48-column positions, the top row has median 92 and P90 188. Full-plane statistics produce a slightly different P90 (192); these are different sample populations, not contradictory results. The lower region is approximately 64. Local 299-to-304 pairs have mean delta Y 176.7-185.3 across all four quartiles and minimum delta Y 168; chroma also changes strongly. This confirms measurable boundary contrast.

**Exact-edge limitation:** row 299 already has median V 610/U 490, indicating red transition/filter fringe. It is not a clean exterior reference. Neither that paired contrast nor lower-bar symmetry proves top=302 is safe. The new route must independently locate both boundaries, retain uncertain transition rows and remeasure side support within the qualified aperture. The preview also contains grain/lighting that may continue across the graphic; distinguishing removable matte from authored artwork remains qualification work.

## Coverage and limits

The capture wrote 30,063 metadata records and 2,145,484,800 native bytes, nearly its 2 GiB frame cap. Native accepted sequences 108881-108992 span approximately 1.852 seconds with 15 unsaved positions. This is not 20 seconds of stored pixels. Presentation metadata covers 1,516 consecutive accepted sequences 108467-109982 / source frames 250000-251515 over 25.27485 seconds, with unchanged source/context identities and no sequence gaps in that presentation series.

The missing native payloads are recording coverage gaps, not proof of playback drops. Recorder lifetime drop counters are cumulative and cannot be assigned to this session. Source identities were not synthesized, recording gaps were not bridged, and old renderer history was not invented. Repeated imagery from a paused example establishes reproducibility, not independent semantic evidence about what may be cropped.

This capture establishes persistent spatial rejection of the reported example. It cannot measure the original scope-to-IMAX/16:9 change onset, prove new transition latency, validate final renderer scheduling, or establish regression-free cropping. No production code/configuration/deployment was changed.

## Reproduction artifacts

The [native replay record](native-replay/native-run-record.json) pins compiler, source and artifact hashes. [Every native payload and sidecar is hashed](native-replay/capture-frame-hashes.csv); the pixels themselves are not committed.

- [Replay source](native-replay/native-ad-replay.cpp), [build/run script](native-replay/run-replay.ps1), [successful build](native-replay/build.log), [all detector results](native-replay/detector.csv), [original edge metrics](native-replay/edges.csv).
- [Diagnostic boundary probe](native-replay/boundary-probe.cpp), [build/run script](native-replay/run-boundary.ps1), [hypothesis safety results](native-replay/candidate-diagnostics.csv), [hypothetical edge metrics](native-replay/hypothetical-retention-edges.csv).
- [Native row support](native-replay/native-row-support.csv), [local boundary pairs](native-replay/native-boundary-pairs.csv), [first full-plane profile summary](native-replay/first-frame-profile-summary.json), [decoder/preview source](native-replay/inspect-native.py).
- [Structured capture/presentation summary](native-replay/transition-summary.json), [presentation rows](native-replay/transition-presentation.csv), [archive manifest](native-replay/archive-manifest.json).

Build/run scripts refer to the original local scratch paths. Their archived bytes are unchanged; no executable, object, raw frame, decoded plane or preview image is in this artifact set.
