# VP-0098: Fit trusted active-picture envelopes correctly on arbitrary CIH screens

## Status

In Progress. On 2026-08-07 the developer confirmed the discovered GitHub
default branch `v1.1.016-beta` as the implementation base and asked that the
implementation continue with explicit review of negative impacts on other
usage, especially NLS.

Implementation is isolated on branch
`codex/vp-0098-arbitrary-cih-envelope` in
`C:\Users\bslac\vp\vp-0098-worktree`, based on remote commit `0dfcbf4`.
The authoritative source checkout has unrelated uncommitted work and remains
untouched.

Readiness review:

- The current configuration model already accepts generic `screen_aspect`
  values from `1.0..4.0`, retains the deprecated `scope_screen_aspect` alias,
  and keeps `automatic_crop`, `subtitle_fit`, hold, release, padding, and
  anamorphic scale as separate settings. No configuration migration is
  required.
- The current pipeline and defect boundary are identified. The renderer's
  vertical bar pass derives source-space `upperRequiredShift` and
  `lowerRequiredShift` from `width / screenAspect`, then may translate a
  same-size trusted source crop. Final NLS and linear layout consume that
  altered crop. This violates source/destination ownership and can discard
  trusted pixels from the opposite picture edge.
- The shared active-picture transition model remains the only crop-authority
  owner. VP-0098 will change presentation-envelope selection and final layout,
  not detector acquisition, pixel-format analysis, or transition confirmation.
- Resource lifetime is bounded to deterministic geometry values owned by the
  existing source and renderer generations; no new GPU, queue, or capture
  resource is required.
- Validation can exercise a pure source-envelope/final-fit seam across the
  required screen/content matrix, then run the native suite and x64 Release
  solution build from the clean worktree.

Compatibility impact watchlist:

- **NLS:** the final NLS source aspect and stretch decision must be derived
  from the same selected presentation envelope as linear fit. Removing
  same-size translation may legitimately change an overlay frame's NLS ratio,
  but screen aspect must not change the source envelope itself and crop/NLS
  authority must still withdraw atomically.
- **Anamorphic profiles:** anamorphic scale remains a destination mapping
  multiplier after source-envelope selection. Tests must prove it cannot feed
  back into detector or envelope geometry.
- **Subtitle/UI behavior:** one-edge overlays will reveal bounded source pixels
  by outward union rather than move a fixed-height window and lose trusted
  pixels at the opposite edge. Existing hold/release timing should remain,
  while translation-specific drift behavior may become obsolete and must not
  cause a visible jump.
- **Full-raster fallback and transitions:** broad/deep, contradictory, stale,
  or unbounded evidence must retain existing fail-open behavior. Confirmed
  full-raster content and source/viewport/renderer generation changes must not
  inherit an old envelope.
- **OSD and diagnostics:** native OSD placement must use the final fitted
  picture rectangle. Final-layout telemetry must expose enough source and
  destination geometry to distinguish authority, envelope, and fit failures.
- **Normal/16:9 use:** automatic crop off and non-CIH profiles must preserve
  their current full-raster behavior. Profile names remain semantically inert.

First implementation slice: add a deterministic source-envelope and centered
fit helper with the exact 3840x2160 / 32:15 reproduction and cross-ratio tests,
then integrate that helper at the renderer's single final-presentation seam.

Implementation progress at source commit `0198dd4` (pushed to the recorded
branch):

- Added deterministic `BuildPresentationEnvelope` and `FitCenteredAspect`
  production helpers. The source helper's inputs contain trusted picture,
  bounded observed content, selected edges, raster, and padding only; there is
  no screen ratio, viewport, NLS, profile name, or anamorphic input.
- Removed the `width / screenAspect` visible-band calculation from the dense
  vertical overlay pass. Required accommodation is now measured outward from
  the trusted picture edge.
- Preserved the existing overlay classification and hold state for transition
  stability, but final rendering converts its legacy one-edge translation
  request into a padded outward envelope. The final crop decision therefore
  reports expansion and never removes the opposite trusted edge.
- Routed linear layout, NLS mapping inputs, NLS fallback, native OSD placement,
  and diagnostics through the same selected source rectangle and centered-fit
  result.
- Added final-layout telemetry containing raster, trusted picture, envelope,
  configured screen aspect, screen rectangle, final picture rectangle, unused
  axis, mapping mode, and anamorphic scale.
- Added the exact customer reproduction plus a matrix covering 4:3, 16:9,
  1.85, 2.0, 32:15, 2.35, and 2.40 screens against representative narrower,
  equal, and wider content. Tests also prove source-envelope independence from
  screen ratio and anamorphic destination mapping.

Validation on 2026-08-07:

- Clean x64 Release solution build passed with Visual Studio 18.7.8.
- Complete native suite passed: 623/623 tests.
- Focused Alpha crop/envelope suite passed: 66/66 tests.
- The recorded 3840x2160 case selects `0,280-3840,1962`, contains the full
  trusted `0,280-3840,1880` picture, and reports only vertical unused space
  when centered inside a 32:15 screen.

Observed compatibility effects and remaining validation:

- NLS now receives the envelope aspect during bounded overlay frames instead
  of a same-height translated-crop aspect. This is intentional and prevents
  NLS from consuming geometry that discarded trusted pixels; the full NLS and
  renderer-state regression suites pass. Live validation must still watch for
  visible NLS-strength changes while subtitles/UI appear and release.
- The existing internal `TRANSLATE` detector/hold label remains for temporal
  compatibility, but the final renderer does not translate the crop. This
  limits code churn and preserves current hold/release evidence ownership;
  diagnostics expose the resulting final expansion explicitly.
- The centered-fit helper reproduces the previous shrink-only layout and the
  normal-profile/NLS-off path keeps its existing output rectangle. Automated
  anamorphic and normal/full-raster tests pass, but live Alpha validation must
  still cover anamorphic on/off and at least one non-32:15 CIH screen.
- Broad/deep evidence, generation invalidation, confirmed full-raster
  authority, provisional retention, P010/P210/native-RGB analysis, and OSD
  placement retain their existing policy paths and pass the complete native
  suite. Customer-class live input remains required before Review.

Deployment checkpoint on 2026-08-07:

- Rebuilt source commit `0198dd4` as a clean x64 Release build; embedded
  version reports `v1.1.016-beta-2-g0198dd4` with `VERSION_DIRTY=false`.
- Re-ran the complete native suite after the clean rebuild: 623/623 passed.
- Preserved the active `C:\Videoprocessor\vp\VideoProcessor.cfg` unchanged.
  It already contains the 32:15 viewport with `automatic_crop: true` and
  `subtitle_fit: true`.
- Backed up the previously deployed executable/plugin pair to
  `C:\Videoprocessor\vp\backup-vp0098-20260807-151859`.
- Deployed both artifacts from the same Release build and verified hashes:
  `VideoProcessor.exe` SHA-256
  `8C950AF27614CCB863D9D3A3E5EA0CA8BCEE258354FA3A109ECC2DDCD0F56217` and
  `vprenderer\VideoProcessorVPRenderer.dll` SHA-256
  `E12D66AC83756466FC176EE121E02EBD0D9284EBA7D61428F3C45CEA67A3FEF7`.
- The application was not running before deployment and was not started
  automatically. Customer-class live validation remains the next action.

## User story

As an Alpha-renderer user with a constant-image-height screen of any practical
aspect ratio, I want VP to identify the active picture and fit all required
visible content inside my configured physical screen, so narrower content has
only side bars, wider content has only top/bottom bars, and encoded bars do not
combine with newly introduced bars to windowbox the image.

## Reported failure

The customer has a 2.8 m wide screen whose measured ratio is 32:15 and uses an
XGIMI Titan projector. The projector is zoomed so the screen surface is the
physical presentation target. With Alpha automatic crop enabled, the customer
reports that content never fills the screen and appears with black space on
all four sides.

The supplied log contains a decisive 24 Hz movie interval:

- Alpha detects a trusted active picture at `0,280-3840,1880`, aspect 2.4000.
- The configured viewport is 32:15 (approximately 2.1333).
- A bounded lower-bar object appears at approximately `1882..1908`.
- The presentation-envelope detector records a bounded bottom expansion.
- The renderer compares that source-space object with bounds derived from the
  destination screen ratio, concludes that no placement is required, and then
  withdraws to the complete `0,0-3840,2160` raster.
- Fitting that 16:9 raster inside 32:15 introduces side bars while the raster
  still contains the movie's encoded top/bottom bars, producing bars on all
  four sides.

The same defect can affect any practical CIH screen ratio. CIH describes a
constant-height presentation strategy; it does not imply a 2.35 or otherwise
"scope-shaped" screen.

## Required geometry contract

Maintain three separate coordinate-space facts:

1. **Encoded raster**: the complete source frame, such as 3840x2160.
2. **Trusted active picture**: generation-current program-picture authority
   established by the shared detector and transition model.
3. **Presentation envelope**: the trusted active picture unioned with bounded
   subtitle or UI pixels that must remain visible, including configured
   padding.

The configured `screen_aspect` describes the physical presentation target.
The final destination layout performs one centered, aspect-preserving fit of
the presentation envelope inside that target.

For a valid nonempty source and target rectangle, ordinary aspect-preserving
fit may leave unused space on only one axis:

- source wider than screen: top/bottom space only;
- source narrower than screen: left/right space only;
- equal aspect: neither axis has unused space.

Bars on both axes are therefore a diagnosable indication that encoded bars
remain in the selected source rectangle, the destination was constrained
twice, or explicitly configured screen insets require that result.

## Source and destination separation

Source detection and source presentation must not depend on the destination
screen ratio. In particular, `screen_aspect` must not be used to decide:

- whether pixels inside an encoded bar are visible content;
- whether those pixels resemble a bounded overlay;
- how far source-space presentation bounds must expand;
- whether trusted crop authority remains valid; or
- whether a source format transition has occurred.

The screen ratio participates only in final destination layout and optional
downstream NLS mapping. NLS is not part of the basic linear-fit correction and
must not be required to obtain correct geometry.

## Presentation-envelope behavior

The trusted active-picture rectangle is immutable until generation-current
detector evidence confirms a real geometry transition. A subtitle, receiver
volume display, menu object, or other bounded object in an encoded bar must
not replace or deepen crop authority.

When bounded excluded-band content must remain visible:

```text
presentation envelope =
    union(trusted active picture, bounded object extent + padding)
```

For the recorded customer frame, the expected presentation is approximately:

```text
encoded raster:       0,0-3840,2160
trusted picture:      0,280-3840,1880
lower object:         1882..1908
bounded envelope:     0,280-3840,1962
physical screen:      32:15
```

The precise aligned boundary is owned by the shared margin/chroma-alignment
policy. The important result is that presentation expands only far enough to
contain the trusted picture and bounded object. It must neither withdraw to
full raster nor translate a same-size source crop in a way that discards valid
pixels from the opposite side of the trusted picture.

## Authority and transition rules

- A confirmed active picture may control `source.crop`.
- A temporary provisional or unavailable observation retains the last trusted
  crop only through existing bounded, generation-owned safety mechanisms.
- Current-frame bounded content outside one trusted edge expands the
  presentation envelope on that edge.
- Broad/deep picture-like evidence or coherent opposing-edge evidence enters
  the format-transition verification path; it does not acquire crop authority
  as an overlay.
- Confirmed full-raster program content withdraws the crop.
- Source, raster, renderer, viewport, and analysis-generation changes discard
  stale geometry and presentation evidence.
- No path may combine vertical translation and outward fit, or manufacture an
  unobserved opposite edge.
- Side-bar inspection remains generic bounded UI/picture evidence. It must not
  assume subtitles normally appear at the sides.

## Configuration boundary

No new configuration is required for the core correction.

- `screen_aspect` remains an arbitrary physical-screen ratio accepted by the
  shared aspect parser over the existing practical range `1.0..4.0`.
- Profile names such as `normal`, `scope`, or `32x15` have no geometry
  semantics.
- `automatic_crop`, `subtitle_fit`, hold, release, and padding retain their
  documented ownership.
- Screen edge offsets may be considered separately for deliberately offset,
  overscanned, or mechanically masked installations, but they are not a
  substitute for correct active-picture fitting and are not required by this
  customer case.

## Implementation direction

1. Extract the source-space overlay/envelope calculation from
   `LibplaceboVideoRenderer.cpp` into a deterministic production helper.
2. Calculate required top/bottom expansion relative to the trusted active
   picture, never relative to destination-visible bounds derived from
   `screen_aspect`.
3. Keep trusted picture authority and presentation envelope as separate
   values through the final crop decision.
4. Replace any same-size crop translation that would remove trusted picture
   pixels with a bounded outward presentation envelope.
5. Feed exactly one selected source rectangle to linear destination fit, NLS
   geometry publication, OSD placement, and diagnostics.
6. Add final-layout telemetry containing raster, trusted picture, envelope,
   configured screen ratio, screen rectangle, final picture rectangle, and
   the axis on which unused space remains.
7. Retain full-raster fail-open only when current content cannot be bounded or
   safely related to generation-current trusted geometry.

## Verification matrix

Automated tests must cover at least these physical screen ratios:

- 4:3;
- 16:9;
- 1.85:1;
- 2.0:1;
- 32:15;
- 2.35:1; and
- 2.40:1.

For each representative screen, exercise active content narrower than, equal
to, and wider than the screen, including 4:3, 16:9, 1.85, 2.0, 2.35, and 2.40
content where applicable. Assertions must prove:

- the final picture is contained within the physical screen rectangle;
- aspect is preserved when anamorphic stretch and NLS are off;
- unused destination space occurs on no more than one axis;
- no trusted active-picture pixel is removed by overlay accommodation; and
- screen ratio does not change source-space detector or envelope decisions.

Transition coverage must include:

- the exact 3840x2160 / 32:15 customer trace;
- lower subtitles and top receiver UI in encoded bars;
- bounded left/right UI evidence;
- simultaneous sparse objects on opposing edges;
- broad/deep opposing-edge picture evidence;
- temporary provisional and unavailable observations;
- genuine full-raster and active-aspect transitions;
- P010, P210, and supported native RGB analysis paths; and
- viewport, source-generation, refresh, renderer, and resolution changes.

## Acceptance criteria

- A trusted 2.40 active picture on a 32:15 screen produces only the expected
  top/bottom destination space, not a four-sided windowbox.
- The recorded lower-bar object produces a bounded envelope containing the
  full trusted picture and padded object without selecting full raster.
- No screen-shape-specific constant or `scope` profile-name inference affects
  source crop, envelope, or final fit.
- Linear fit works for every tested practical CIH screen ratio without NLS.
- NLS-off and NLS-unavailable paths preserve the same correct screen and
  source geometry contract.
- Bounded ambiguity handling does not flash repeatedly between trusted crop
  and full raster during ordinary subtitles, menus, or receiver overlays.
- Confirmed full-raster content and genuine aspect transitions remain
  fail-safe and do not inherit stale crop or envelope state.
- Diagnostics make all four-sided-bar incidents independently explainable and
  identify whether source authority, presentation envelope, or destination
  layout caused the result.
- A clean x64 Release solution build and the complete native test suite pass.
- Live Alpha validation succeeds on the customer's 32:15 class of geometry
  and on at least one other CIH ratio before completion.

## Dependencies and related work

- VP-0038 owns generic arbitrary-aspect viewport state and screen-aware NLS.
- VP-0040 owns trusted active-picture authority.
- VP-0062 owns safe full-raster fallback for ambiguous/high-black content.
- VP-0080 owns fail-safe Alpha crop authority and existing asymmetric-overlay
  presentation behavior. This story narrows the new defect to arbitrary-screen
  source-envelope and final-fit correctness rather than reopening authority
  acquisition generally.
- VP-0082 provides buffered active-picture look-ahead.
- VP-0070 remains separate subtitle capture/relocation work; this story only
  preserves already-rendered source pixels.

## Non-goals

- Configuring madVR, projector zoom, lens memory, curtains, or masking motors.
- OCR, subtitle replacement, or glyph relocation.
- Requiring or redesigning NLS to make ordinary aspect-preserving fit work.
- Treating CIH as synonymous with any particular screen aspect.
