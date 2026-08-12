# VP-0122: Retain scope geometry through subtitle and volume overlays

## Status

In Progress (updated 2026-08-12). Implementation commits `dcd2588` and
`46aa1e5` are pushed on `codex/vp-0122-overlay-geometry`. The first deployed
build reproduced a remaining scene-hold expiry at 23:52:44: an accepted held
translation was still valid, but its older source sequence prevented the cut
frame's direction-matched current envelope from attesting it. Commit `46aa1e5`
uses the fresh same-generation envelope instead, restricted to the translated
edge with unchanged horizontal and opposite-edge geometry. The exact commit
built x64 Release, passed 94/94 focused policy tests and 802/807 full-suite
tests; the five failures are the existing configuration/reference fixtures.
The verified 55-file package is deployed to `C:\Videoprocessor\vp` with the
active configuration preserved. Awaiting live operator validation of the same
overlay/scene-transition sequence.

## User story

As a scope-screen operator, I want subtitles and receiver volume overlays in
the encoded black bars to remain visible without temporarily shrinking the
entire picture, so transient overlays do not create distracting black bars on
all four sides.

## Problem

Live logs captured two related failures while the underlying picture remained
the trusted scope rectangle `0,276-3840,1884`:

- At 22:50:39, a top volume overlay coincided with ambiguous bottom-band
  pixels. A single dense scan selected `FIT`, expanded presentation to the full
  raster, and briefly produced a centered 16:9 picture with four-sided bars.
- At 23:19:05-07 and 23:19:29-30, sustained bottom subtitles outlived a
  provisional bar-authority/scene snapshot. The trusted scope geometry became
  unavailable until it was reacquired, causing one-to-two-second full-raster
  fallbacks.

Short top and bottom overlays otherwise translated correctly. Manual renderer
resets caused separate authority cold starts and are not part of this defect.

## Required behavior

1. Treat localized top and bottom overlay-like content symmetrically. Either
   edge may contain a subtitle, receiver volume UI, or similar transient text.
2. An overlay may alter bounded vertical presentation translation, but it must
   not independently revoke established same-generation picture geometry.
3. Preserve an established trusted crop through an overlay-induced scene
   boundary or provisional authority gap while current dense evidence remains
   overlay-like and compatible with the trusted rectangle.
4. Do not let darkness or an uninspected timer renew crop authority. Continued
   retention requires fresh, same-generation, frame-local overlay evidence.
5. Require consecutive/persistent picture-like evidence before a transient
   two-edge observation may replace an established crop with outward `FIT`.
6. Preserve immediate fail-open behavior for trusted full-raster evidence,
   source/raster generation changes, invalid analysis input, incompatible
   geometry, or sustained broad/deep live pixels outside the crop.
7. Keep presentation stable while confirmation is pending. Prefer a few extra
   cropped frames over cycling between scope and four-sided bars.
8. Log when geometry is retained by overlay evidence, when Fit confirmation is
   pending, and which evidence finally confirms or rejects Fit.

## Acceptance criteria

- Replaying the captured long bottom-subtitle sequences produces no
  full-raster/four-sided final layout and retains `0,276-3840,1884` throughout.
- A top volume overlay is handled through the same overlay path as a top
  subtitle and never causes a one-frame Fit by itself.
- Overlapping top and bottom overlay-like content uses a deterministic stable
  translation decision without Fit or oscillation.
- One contradictory picture-like scan cannot trigger Fit; disappearing
  evidence returns to/continues the trusted presentation without a layout
  cycle.
- Sustained broad/deep content outside both trusted bars eventually confirms
  Fit within a documented bounded interval.
- A trusted scope-to-16:9/full-raster transition remains immediate or within
  its existing trusted-transition latency.
- Invalid input, new source/raster generation, and incompatible geometry still
  fail open rather than hiding potentially live pixels.
- Focused active-picture/source-crop tests and the complete x64 Release test
  suite pass.

## Regression fixtures

Add deterministic tests for:

- trusted scope -> short bottom subtitle -> scope;
- trusted scope -> bottom subtitle lasting beyond the previous two-second
  scene/ambiguity hold -> scope;
- trusted scope -> top volume/subtitle overlay -> scope;
- retained bottom translation plus top overlay;
- top overlay plus a single picture-like bottom sample;
- overlapping top and bottom overlay-like content;
- sustained broad/deep two-edge picture fill;
- genuine trusted full-raster and scope-to-16:9 transitions;
- manual/source-generation reset as a separate cold-start case.

## Implementation notes

Keep geometry authority, frame-local visibility safety, and presentation
translation as separate concepts. The change belongs in the scene snapshot /
provisional-gap retention policy and vertical Fit arbitration, not in the P010
pixel extractor and not in NLS shader selection.

## Non-goals

- Identifying overlays by application, OCR, logo, or neural network.
- Preserving crop across an actual source/raster generation change.
- Changing user-configured subtitle drift/hold timing except where necessary
  to prevent geometry authority from expiring underneath current overlay
  evidence.
