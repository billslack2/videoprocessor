# Independent primary-source crop review

Reviewed2026-11-01. Design research only; no imported implementation/runtime edits. Upstream links reflect current source today; pin revisions before implementation comparisons.

## mpv

The official script delays4seconds, gathers1second of lavfi cropdetect metadata, applies video-crop and removes the detector. Its defaults include reset0, limit24/255, divisibility2 and minimum width/height ratios0.5. It explicitly warns against choosing blackframes, titlecards or darkscenes, and temporarily disables incompatible hardware decoding. This is a static/operator-assisted workflow, not continuous dynamic-AR proof. Useful ideas: detector metadata separate from application, explicit bounds and alignment. Its delay/manual retry/detector removal are unsuitable for live VP transitions. Source behavior is direct observation; applicability is our inference. [Primary mpv source, lines1-45,114-134,186-211](https://github.com/mpv-player/mpv/blob/master/TOOLS/lua/autocrop.lua)

## HandBrake

Its8-bit scan helpers clamp low luma to16, require average darkness below32 and per-pixel deviations within16. Rows inspect all pixels; columns inspect the vertically cropped region. Preview crop distances are sorted peredge. Loose/median/tight modes select different protection/aggression tradeoffs; smart selection considers substantial deviations across previews. This offline title-level aggregation does not establish instantaneous liveAR or subtitle separation. Useful concepts are matte coherence, pixelcoverage and explicit mixed-aspect policy. Do not transplant8-bit constants, independent edgequantiles or offline preview counts into10-bit HDMI. [Primary scan.c, lines423-535,1308-1380](https://github.com/HandBrake/HandBrake/blob/master/libhb/scan.c)

Official documentation calls autocrop a bestguess and provides manual correction/preview, so agreement with it is not groundtruth. [Official cropping documentation](https://handbrake.fr/docs/en/latest/advanced/resizing-video.html#cropping)

## Dangerous oversimplifications in our replacement stages

The shared evidence -> geometry reconciliation -> temporal association -> separate guard/layout model is sound ownership, subject to:

1. One evidence owner cannot mean one scalar threshold. Sourcefloor/noise, discard tolerance and picture-presence contrast are distinct. Reusing discardthreshold asfloor caused the40-code doublemargin. Range/precision must be explicit for native V210 and decodedDV without guessing displaybrightness.
2. Retain edgehypotheses, intervals and competing boundaries until reconciliation. Averaging two genuineedges invents another. A valid top must survive an uncertain bottom, while a completeAR reflects joint uncertainty. Retention may independently preserve a larger aperture.
3. Global reconciliation must inspect exterior components/layout, not merely dense rows. A bounded occupancy sketch plus native refinement addresses gridmenu avatar/nav/logo samplingmisses. Do not erase thin realpicture strips. Text morphology is evidence, not a universal menu/subtitle oracle; static black layoutmargin is not automatically matte or proven fullraster.
4. Temporal association must use exact measured sources rather than smoothing. Owned queued confirmations can act on first displayedsource. Offline medians/1second accumulations hide expansion; indefinite unions prevent inward changes. Preserve identity/domain/generation/continuity, resets and retirementwitnesses. Repeated display adds no measurementvote; separately scanned undisplayed inputs can contribute rawfacts without display/cue lease.
5. Expansion and inward discard intentionally have different safety requirements. Current broad expansion can protect pixels immediately; inward crop requires affirmative exterior-matte proof. This does not require waiting Ndisplayedframes. Known ratios/history nominate exact current tests, never manufacture missing coordinates or snap results.
6. Separate outputs share facts, not authority. Precise physicalmeasurement can coexist with an even/chroma-safe larger discardguard. Caption cleanup, fitting and fill must not overwrite physicalAR. OSD must not announce an optional unverified internal edge as stronger truth than source geometry. Preserve the working subtitle ownership/erasure path.
7. Live VP cannot stop hardware decoding or remove its detector like mpv. Bound sketch/refinement cost, preserve sourceversion caches and explicit failures. Performance acceptance includes faithful arrivals and firstcorrectoutput latency.

## Comparison contract

Upstream outputs are independent diagnostic observations, not expected labels. Compare unrounded measured coordinates before application rounding. Record source/range conversion, settings, frame identities and reset policy. Evaluate dark/bright scope, uneven bottom light, sparse gridheader, raised/patterned matte, captions/logos, real mixedAR and unresolved intervals. Report missedbars, falsecrops, edgeerror, caption preservation and first safe source, not only agreement with an offline cropper.
