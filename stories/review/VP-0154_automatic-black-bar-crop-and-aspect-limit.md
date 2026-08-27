# VP-0154: Automatic black-bar crop and aspect-limit fill

## Status

Review (2026-08-27). Implemented on
`codex/vp-0154-black-bar-crop-limit` at `3d1aa71`, rebased on current
`v1.3.001-beta` tip `85abed0`. The new optional
`automatic_crop_aspect_limit` preserves the literal value entered in Config
(for example `2.20:1`), while using a separate numeric representation only
for rendering. It applies a centered top/bottom fill only after trusted
automatic black-bar crop, only when the trusted content aspect meets the
configured threshold, and never for NLS source geometry. Omission retains
ordinary aspect-preserving fit. Focused geometry and configuration tests
passed from a clean x64 Release test build; x64 Release GUI and Config editor
test targets built successfully. The branch is pushed for operator review.
Do not merge or deploy until approved.

## User story

As a VideoProcessor operator using a scope screen, I want automatic black-bar
crop to visibly zoom the trusted active picture and an optional aspect-ratio
limit to bound when VP may fill-and-crop it to the screen, so scope material
uses the screen without risking a large crop of narrower content.

## Scope

1. Diagnose and repair the operator-visible no-op in automatic black-bar crop
   while preserving trusted active-picture authority and fail-open behavior.
2. Add an optional per-viewport **Aspect ratio limit**. It is a content-aspect
   eligibility threshold: when trusted cropped content is at least the limit
   but narrower than the configured screen, VP may symmetrically fill the
   screen and crop the opposite source edges. With no limit, retain the
   existing aspect-preserving fit after black bars are removed.
3. Make the control clear in Config, persist it in the viewport profile,
   validate ratio/decimal input, apply it live, and document its crop effect.
4. Add focused geometry and configuration tests plus diagnostics that report
   detected source aspect, configured screen aspect, optional limit, selected
   source crop, and the reason a fill was applied or withheld.

## Acceptance criteria

- Trusted letterbox or pillarbox bars are removed and the active picture is
  scaled into the configured physical-screen viewport.
- With `automatic_crop_aspect_limit: 2.20:1` and a wider configured scope
  screen, trusted 2.20:1 content may be centered, zoomed, and symmetrically
  cropped to fill; narrower content is fitted without this extra crop.
- Omitting the limit retains existing aspect-preserving automatic-crop
  behavior.
- No crop is manufactured from full-raster, provisional, stale, or
  contradictory evidence; subtitle/overlay safety remains intact.
- Configuration UI, parsing, profile inheritance, live application,
  diagnostics, documentation, focused tests, and a clean x64 Release build
  agree on the same semantics.

## Boundaries

- Do not copy madVR controls or alter madVR behavior.
- Do not use aspect limits to bypass active-picture evidence.
- Do not modify NLS mapping or physical screen calibration except where the
  selected final source rectangle must be represented accurately.

## Related work

- VP-0080: Fail-safe Alpha active-picture crop
- VP-0098: CIH active-picture envelopes and physical-screen fit
- VP-0114: Conservative scaling and small-bar zoom controls (related but
  distinct destination-line thresholds)
- VP-0131: Bounded presentation crop

## Tracker audit note

- Created after fetching `origin/main` and auditing 171 canonical records on
  2026-08-27. `VP-0152` was the highest assigned root ID.
