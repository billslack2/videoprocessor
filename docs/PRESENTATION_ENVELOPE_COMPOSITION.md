# Complete presentation envelopes before union

## Reproduced problem

In the 2026-09-22 16:19:15 replay on build `4105fafc`, renderer instance
`{C23927F7-3D2B-49F0-8383-6EE400B4FCEA}`, sequence 1047 successfully handed an
expired 192-pixel subtitle translation to freshly confirmed FIT. Logical scope
remained `0,276-3840,1884`. The detector envelope was `0,54-3840,2106`, while the
raw dense extent was `68..2092`. The active dense margin was 34 pixels:
`max(8,2160/90) + 10` configured padding.

The renderer unioned these measurements first and padded that union, producing
`0,20-3840,2140`. At sequence 1061 it accepted logical `0,68-3840,2092`, tightening
the image again 14 frames (about 584 ms at 24 fps) later. The prior handoff fix worked;
this was a separate presentation-padding effect.

## Consistent composition rule

`BuildComposedPresentationEnvelope` is now used by the renderer itself:

1. Complete the detector's selected edges against the trusted picture, without
   adding subtitle padding. Keep any safety extent supplied by its producer.
2. Complete raw dense measurements against the same trusted picture with the
   existing horizontal and vertical subtitle margins, including their minimums.
3. Union the completed envelopes without another padding pass.

For the recorded case the dense result is `34..2126`; the detector's `54..2106`
fits inside it. The final presentation is therefore `0,34-3840,2126`. This still
includes deliberate dense clearance; it does not force presentation to the raw
`68..2092` measurements or prematurely accept them as logical aspect geometry.

Both components start at the trusted picture. Each has independent selected
edges; vertical edges suppressed by subtitle-translation routing cannot leak
back through a simultaneous horizontal FIT. Active malformed rectangles fail
validation. Inactive components are ignored. Existing chroma alignment and
raster clamping remain in the common geometry builder.

The intentional broader behavior change is **detector-only clearance**. A
selected detector edge retains its supplied extent but no longer receives the
extra dense subtitle margin. This applies equally to raw and already protected
detector bounds. It preserves measured coverage, not a guarantee that every
physical content pixel was detected. Sparse controls and menus therefore remain
part of playback validation.

## Scope and diagnostics

No changes were made to detection thresholds, aspect deadbands, subtitle
recognition, evidence confidence, confirmation counts, lookahead, source/base
checks, stored-envelope lifetimes, or final presentation admission. Dense-only
geometry keeps the existing padding behavior. The compositor adds a fixed number
of rectangle operations; it does not scan pixels or introduce temporal state.
The normalized source-coordinate path is shared by native and converted inputs;
there is no new P010-specific branch or conversion.

Normal logging adds `Alpha envelope composition`, only alongside an existing
crop-policy change when an envelope is expanded or invalid. It records renderer
instance, generation, source sequence, viewport epoch, selected detector/dense
edges, their bounds, dense padding, and composed result. Unselected rectangle
edges are context only. `invalid=1` means the composition failed and its result
field is the renderer's unchanged base placeholder; consult `Alpha source crop`
for the actual fail-open presentation. Existing layout logs remain authoritative
for the final displayed rectangle.

The change removes unnecessary clearance; it does not eliminate the 14-frame
weak-evidence delay observed in this replay. Fresh viewing must assess whether
the remaining move to accepted IMAX framing is acceptable.

## Verification

The renderer assembly was first extracted without changing its union-then-pad
calculation. Independent new tests ran against that baseline: seven expected
failures and one dense-only pass, including expected top34 versus actual20 in
both the compositor and final admission. Evidence:
`artifacts/lookahead/envelope-composition-red.trx` and the preserved legacy
helper in `artifacts/lookahead/envelope-composition-legacy-helper.txt`.

The expanded GREEN suite passes **13/13** in
`artifacts/lookahead/envelope-composition-green.trx`. It covers the recorded
bounds, detector-only clearance, unchanged dense padding at three resolutions
and configured padding 0/10/20/500, active-invalid/inactive evidence, all 256
pairs of selected-edge masks, inward/no-component behavior, and raster/chroma
limits. Sequence tests combine the production compositor and lifetime policy
with modeled renderer accumulation; they do not instantiate the GPU renderer.
A provisional-frame test exercises actual inspection, resolution, crop policy,
and final admission, including refusal to acquire a new crop from that FIT.

The full native suite passes **1,443/1,443** in
`artifacts/lookahead/envelope-composition-green-full.trx`. All 1,430 existing
tests are unchanged; added coverage lives separately in
`PresentationEnvelopeCompositionTests.cpp`.

Independent implementation and QA reviews found no blocking issue. The final
x64 Release solution build uses `MSBuild VideoProcessor.sln /m:4
/p:Configuration=Release /p:Platform=x64`; its log is
`artifacts/lookahead/envelope-composition-final-release-build.log`.
No GPU playback validation or deployment is included in this change. Replaying
the reported transition, then ordinary subtitle/menu and aspect changes, remains
the next validation step.
