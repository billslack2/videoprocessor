# VP-0098: Fit trusted active-picture envelopes correctly on arbitrary CIH screens

## Status

Backlog. Created 2026-08-07 from a reported Alpha-renderer failure on a
physical 32:15 screen and a matching customer debug log. Investigation has
identified a coordinate-ownership defect and established a proposed geometry
contract, but production implementation has not started under this story.

The current source worktree contains an intentionally failing reproduction
test created during diagnosis. Before implementation starts, discover the
current `billslack2/videoprocessor` default branch, obtain developer
confirmation of the implementation base, and create or designate a clean
story-specific branch/worktree as required by the tracker workflow.

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
