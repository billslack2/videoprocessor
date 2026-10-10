# Subtitle evidence and aspect presentation

This change uses fresh subtitle measurements to verify when an already admitted
picture can be displayed without the expanded envelope that protected the original
caption. It never acquires native source aspect authority from moved text, generated
backgrounds, or cleanup pixels. A separate cold-start presentation path can
nominate a guarded aperture from independently measured source boundaries when
Classic has no cropped authority; it cannot teach native geometry history.

## Evidence order

1. Raw source analysis, the AR transition model, recovery, and crop admission run
   unchanged. Their original decisions and learned geometry remain authoritative.
2. The subtitle worker supplies current source identity, physical picture planes,
   measured text, and owned ink. The accepted cue and production capture geometry
   must agree with that measurement. Held, partial, stale, pending, or work-limited
   observations cannot supply the certificate.
3. Successful black/generated subtitle composition may nominate the established
   logical crop for final presentation. The helper verifies exact prior admission,
   current source/format/viewport/renderer/policy/continuity identity, complete
   destination containment, and actual source erasure coverage.
4. Strict existing bar inspection plus a bounded native-pixel audit must show that
   every excluded source pixel outside the actually erased caption remains black
   and neutral. Residual glyph tolerance and generated picture cleanup do not
   establish source boundaries. Genuine expansion or an unexplained bright/colored
   pixel rejects the adjustment.
5. Only final display bounds change. Shared detector evidence, history, admission,
   and generic fit tracking retain their original values. Optional further fill is
   withheld for this presentation so it cannot crop the certified destination.

The adjustment is limited to an admitted vertical outward fit with a previously
accepted full-width logical picture. It does not override source discontinuities,
scene verification, moving-picture transitions, near-black recovery, fixed crop,
NLS, or competing protection owners. Rejection uses the normal existing display.

## Continuity

Loss of a previously successful certificate widens safely and inhibits further
relaxation in the same geometry/context until independently measured ORIGINAL
source bands are clear. Changing a cue, a duplicate presentation, or a skipped
frame cannot rearm it. This prevents alternating full/held subtitle measurements
from repeatedly shrinking and widening the image. No missing measurement permits
blind retention of a narrower crop.

## Scene diagnostics

A separate 32 by 18 source-grid comparison reports changes with and without
current owned subtitle samples. Both frames use the same union of exclusions;
at least 75 percent of samples must remain. Duplicate source sequences do not
produce extra comparisons. Source identity, policy, and continuity changes reset
comparison eligibility. AR-conditioned subtitle measurements are identified in
the diagnostic reason. The authoritative scene detector and its thresholds are
unchanged.

## Telemetry and validation

`SUBTITLE AR EVIDENCE` reports logical/shared/display bounds, original erased
footprint, moved destination, current measurement/composition status, source
identity, rejection reason, sample count and time. `logical_mutation=0` makes the
presentation-only contract explicit.

`SUBTITLE SCENE EVIDENCE` reports the original and subtitle-excluded differences,
remaining sample coverage, eligibility, and authoritative scene events side by
side. `diagnostic_only=1` means no cut decision is changed by that score.

Native regressions cover production subtitle geometry, both bars, crossing text,
P010/P210, current ownership, failed composition, stale context, one-pixel and
colored unexplained content, real expansion, insufficient coverage, and the
continuity latch. A 4K timing test records cost without a brittle timing assertion.
Playback observation still needs actual caption/format transitions; a successful
build and synthetic tests do not certify all HDMI content.

## Cold-start source-assisted presentation

The subtitle worker may independently nominate a full-width vertical aperture
only when Classic has no available cropped picture and the current renderer
policy explicitly permits nomination. Withdrawal alone is never permission;
foreign contexts, disabled styles, transitions and recovery cannot opt in.
Permission changes invalidate queued measurements. Both boundaries must have
current contrast support distributed across at least three horizontal zones and
both halves of the raster. Exactly one edge must already pass native bar checks.
The opposite edge is measured independently; symmetry or a single rightmost
column cannot establish its location. The candidate moves outward by one subtitle
sampling step to retain the native picture fringe. Its axes remain NONE.

The worker nomination is explicitly marked assisted and never seeds normal bar
tracking, pending-measurement continuation, scene evidence, or native presentation
certificates. The rendering thread re-nominates from the current original source
and requires exact agreement, current frame/policy/continuity identity and fresh
complete glyph morphology before using the measurement for composition.

A bounded P010/P210 native-pixel audit checks the entirety of both excluded bands.
All pixels must be neutral. Nonblack pixels may be explained only by current owned
glyph samples, expanded by at most sampling-step plus one native pixel (maximum
five), and contained in the exact production glyph capture footprint. A raw-black
moat separates the excluded bands from the picture; picture-side glyphs cannot
supply masking authority outside that moat. Unowned content, chromatic growth,
insufficient spatial support, missing/stale evidence, work limits, and excessive
coverage all reject the proof. No bounding-box, cleanup-panel, or synthetic-pixel
mask is used. Morphology remains a visual heuristic, not semantic text recognition.

Four consecutive distinct source frames with the same guarded geometry are
required. Missing proof or geometric jitter after confirmation withdraws the
assisted crop and inhibits reacquisition until independently clear raw bands or a
real source/policy context reset. Duplicate presentations cannot add votes.
Successful current subtitle composition may release the original caption envelope;
otherwise a proven original-caption envelope remains protected. A separate latch
prevents intermittent composition success from repeatedly narrowing the crop.

This only selects final linear presentation while the normal shared decision is
full raster. Fixed crop, NLS, picture transitions and near-black recovery retain
their existing authority. Optional fill is withheld, native history/admission and trusted HDR picture
authority remain unchanged, and the result is never advertised as native trusted AR.
`SUBTITLE ASSISTED AR` logs nomination, proof, confirmation, inhibition, bounds,
composition, audit counts and elapsed time with `logical_mutation=0 history_learning=0`.

The paused 3840x2160 input supplied during development independently nominates
0,274–3840,1888; the proof audits 2,096,640 excluded native pixels and accounts for
16,734 subtitle pixels. Cropping at the unguarded top278 would remove real picture
fringe. This single-frame replay is a regression fixture for diagnosis, not a
claim that every subtitle or aspect transition is supported. The initial cold
path deliberately declines existing cropped authority, both contaminated edges,
and glyphs touching the raw-black moat.

### Unclassified near-black startup

A blank input at startup can latch the existing full-raster near-black episode
before any picture has been classified. The episode now records whether it began
without crop or full-raster authority; losing a previously trusted reference or
changing the viewport cannot manufacture that origin.

Only that unclassified episode may collect assisted evidence after fresh original
pixels are independently non-near-black. The same exact-frame predicate gates
worker permission, rendering-thread proof, and final presentation. Four current
proofs may select a presentation-only overlay while the native episode remains
FULL_RASTER. No episode reset or history publication occurs. Existing admission,
recovery, transition, scene, fixed-crop, NLS, and fail-open exclusions still apply.
Missing proof immediately returns to the episode's full raster. Telemetry labels
this exception `startup_overlay=1`.
