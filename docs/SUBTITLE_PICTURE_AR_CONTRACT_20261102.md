# Processed subtitles preserve the measured picture aspect

This contract supersedes historical rules allowing Classic confirmation/engage or
unprepared advanced output to widen the movie aperture to contain raw captions.
Those fallbacks preserve glyphs by shrinking the movie and violate subtitle processing.

## Required behavior

- Derive native picture boundaries and aspect from source pixels. HDMI raster,
  physical screen, CIH, fill limits, zoom and subtitle bounds are not measurements
  of the movie aspect. An independently proved picture transition still wins.
- Off can retain a larger raw picture-plus-caption aperture.
- Classic, Black and Reconstruction use the same native-sized movie canvas for
  the same native picture. A confirmed subtitle cannot change its width or height
  at confirmation, engage, retarget, release, a skipped scan or a profile switch.
- When Classic has both top and bottom bar content, bottom content wins. Keep the
  native movie dimensions and let top content fall outside that window. Current
  occupied-side evidence can select this pose without pretending UI is a confirmed
  caption. Prior upward pan, confirmation delay and skipped scans cannot reverse
  this priority. Independently proved picture expansion still wins.
- Classic constrains the actual pan to an interval containing the complete current
  caption, including padding and ordinary configured fill. This constraint does
  not rewrite controller targets, timing, votes or lease expiry. Display repeats
  must obey the same source-specific interval.
- Black/Reconstruction need matching current composition or prepared presentation
  evidence for completed caption output. Preserve the existing initial shader-build
  exception: native-sized playback can continue while subtitle shaders prepare,
  with no caption completion or cleanup claim. Initial clipping was already allowed
  by the operator; shader warmup is never permission to widen the movie.
- A positively identified caption awaiting required processing can withhold its
  source as a whole; it cannot publish partial composition or cleanup. UNKNOWN
  inspection alone is not proof of an unfinished caption. Classic can advance
  the current raw source when exact-current occupancy constrains a compatible
  fixed-size viewport, including configured fill. This continuation grants no
  caption completion/absence, native picture, FIT, erasure or composition proof.
  Stale or infeasible evidence cannot authorize a new pan. Shader preparation
  and source acquisition continue while an actual processing requirement waits;
  an acquired library target is released without presenting partial output.
- Current empty-band evidence can permit the fixed canvas without inventing a
  semantic caption-absence observation. A current text candidate remains a pixel
  preservation constraint even before semantic confirmation.
- Completion is bound to generation, source, viewport, format, analysis domain,
  native base and mode. A source snapshot can complete once without changing its
  original capture time. Stale packets cannot renew caption or picture authority.

## Evidence and acceptance

The deployed 129457b0 replay still shrank. Its Classic incident at
16:00:50.408Z G1 O366 S217 retained native [0,256,3840,1904] but submitted
[0,256,3840,2014] on initial caption entry. Reconstruction later submitted wider
raw apertures when composition/inspection was unavailable. The user saw the issue
both during playback with controls hidden and when pausing/showing controls.
The previous pending-retarget repair therefore did not satisfy this contract.

The private logs, state and Classic screenshot pair are preserved in
E:\codex\videoprocessor\evidence\reconstruction-postfix-20261102-1101.
No Reconstruction screenshot was taken: Classic's 20-second cooldown suppressed
its later event. Capture now cancels old followups and rearms cooldown on requested
profile/context changes, while retaining the global 12-image session limit.
An intentional profile switch is not itself a failure.

Native counterexamples check stale identity, missing caption bounds, mismatched
profiles and configured-fill coverage. Actual-renderer regressions reject temporary
wider caption envelopes and still require complete caption coverage, normal movement,
24 distinct stable sources, genuine measured transitions and both shipped dependencies.
Temporal/source lifetime changes additionally require the subtitle-motion suite on
both dependencies. Source-compatible qualification and deployment do not establish
live acceptance: replay the captured scene after deployment and compare timecoded
submitted output, native bounds, caption state and processing disposition.

Prolonged withheld output is a failure, not a successful constant-AR result. Native
acquisition, missing analysis, UI overlays, both-bar occupancy and failed preparation
must remain visible in telemetry and must be covered before runtime acceptance.

Telemetry distinguishes Off (0), Classic (1), Black (2) and Reconstruction (3) in
`subtitle_processing_profile`, independently of requested/effective shader mode.
Submitted-presentation metrics 6/7 carry the source-specific minimum/maximum pan
position so the constrained actual output can be compared to controller intent.

When both Classic bars are occupied, retain an already submitted safe downward pan
rather than jittering with coarse extent sampling. Current newly lower content can
increase that pan. Top content never forces an upward pan or a wider aperture.
