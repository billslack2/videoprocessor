# Subtitle evidence and aspect presentation

This change uses fresh subtitle measurements to verify when an already admitted
picture can be displayed without the expanded envelope that protected the original
caption. It does not acquire a new source aspect ratio from moved text, generated
backgrounds, or cleanup pixels.

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
