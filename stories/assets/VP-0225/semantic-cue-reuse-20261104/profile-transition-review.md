# Profile-cycle recording correlation

Input: C:\Users\bslac\Videos\2026-11-04 13-12-32.mp4, 2692 frames at 60 fps (44.8667 s). The encoded player timecode, not the filename alone, supplies the joins below.

Confirmed same-context Black startup instability, instance43741ABE generation2:
- Video22.00s: S150, 18:12:54.602Z, large raw caption and smaller movie aperture.
- Video22.20s: S155, 18:12:54.812Z, composed caption, larger movie aperture.
- Video22.35s: S159, 18:12:54.978Z, raw again.
- Video22.70s: S167, 18:12:55.316Z, still raw.
- Video22.90s: S172, 18:12:55.529Z, composed again; S181 at55.898 remains composed.

The exact logs match Black raw12/composed3/raw12/composed26, with native picture bounds unchanged. See transition-source-crops.jpg and black-transition-contact.jpg. This verifies the user's first-second size/backing/framing pulse; it does not establish subtitle-text absence.

The final Reconstruction interval is stable: checked video38s S534 at18:13:10.619Z and40s S582 at18:13:12.624Z have the same composition and movie geometry. Logs show150 submitted composed sources522..671 through18:13:16.336Z. Later rows672..772 are rendered-only/submitted=false; the S771 composition miss is therefore not evidence of a visible flash. An initial contact-sheet impression of flips in this final interval was retracted after exact source/timecode inspection.

Fullscreen/windowed renderer recreation and explicit Classic/Off changes occur elsewhere in the recording. They must not be counted as autonomous steady-Reconstruction crop oscillations. First cold Reconstruction acquisition examples take approximately0.53–0.89s from first current output to first composed output; a warm switch into Reconstruction atS522 composes immediately.

The automatically saved after-output-817.png is a103-byte1x1 placeholder despite1000x563 sidecar dimensions. It is not visual evidence. Capture validation must compare encoded image dimensions with metadata before declaring an image usable; no source change for that separate issue is included here.

Source/code review: RC4 retained semantic cue/layout history across grouping noise, kept current pixel matching separate from display geometry, and froze unchanged-cue backing panels. The current worker proposal restores eligibility for this presentation history from qualified exact-current sampling/composition evidence, without native/crop/absence permission. Mode changes still require exact current mode/context proof; no previous composed pixels may be replayed. This report does not claim the new worker patch resolves transition behavior before runtime validation.

