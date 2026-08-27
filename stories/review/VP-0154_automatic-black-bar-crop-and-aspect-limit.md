# VP-0154: Automatic black-bar crop and aspect-limit fill

## Status

Review (2026-08-27). Implemented on
`codex/vp-0154-black-bar-crop-limit` at `6f376d4`, rebased on current
`v1.3.001-beta` tip `85abed0`. Config now has two independent controls, both
off by default: **Crop narrower content to fill screen** and **Crop wider
content to fill screen**. Each has its own optional Aspect ratio limit and
preserves the literal value entered (for example `2.20:1`). A blank narrower
limit allows any trusted narrower content; a blank wider limit allows any
trusted wider content. A narrower limit is the minimum eligible aspect, while
a wider limit is the maximum eligible aspect. Both operations require current
trusted active-picture authority: an automatic bar crop or a generation-current
trusted full raster. They are centered (top/bottom for narrower, left/right
for wider), and are disabled for NLS source geometry. The existing pipeline
preparation status popup is enabled so GPU pipeline compilation no longer looks
like an unexplained black screen. Focused geometry tests passed from x64 Release
builds, and fresh x64 Release GUI and VP Renderer builds succeeded. The
verified x64 Release package from `ee17077` was deployed to
`C:\Videoprocessor\vp` for operator testing; its host and renderer hashes
matched the package, and the active `VideoProcessor.cfg` remained unchanged.
The latest review commits also make newly entered profile overrides immediately
use the normal bright field colour, label the profile currently being edited as
active or not active, and apply Screen Config profile edits through the live
profile path used by F2/F3 rather than restarting the renderer; they have not
yet been deployed. The branch remains unmerged and in Review.

## User story

As a VideoProcessor operator using a scope screen, I want trusted active-picture
geometry to visibly zoom the picture using independent narrower-content and
wider-content choices, each with an optional aspect-ratio limit, so I decide
which content may be cropped to fill the screen.

## Scope

1. Diagnose and repair the operator-visible no-op in automatic black-bar crop
   while preserving trusted active-picture authority and fail-open behavior.
2. Add independent per-viewport narrower-content and wider-content fill
   choices, each with an optional **Aspect ratio limit**. A narrower limit is
   a minimum aspect; a wider limit is a maximum aspect. With a blank limit,
   that enabled direction may crop any current trusted content to fill.
3. Make the two controls clear in Config, persist their literal ratio/decimal
   input in the viewport profile, apply them live, and document their effects.
4. Add focused geometry and configuration tests plus diagnostics that report
   detected source aspect, configured screen aspect, optional limit, selected
   source crop, and the reason a fill was applied or withheld.

## Acceptance criteria

- Trusted letterbox or pillarbox bars are removed and the active picture is
  scaled into the configured physical-screen viewport. A generation-current
  trusted full raster is also eligible for explicitly selected fill.
- With `crop_narrower_content_to_fill_screen: true` and
  `crop_narrower_content_aspect_limit: 2.20:1`, trusted 2.20:1 content on a
  wider scope screen may be centered, zoomed, and symmetrically cropped
  top/bottom to fill; narrower content remains fitted.
- With `crop_wider_content_to_fill_screen: true`, trusted content wider than
  the screen may be centered and symmetrically cropped left/right to fill.
  `crop_wider_content_aspect_limit` optionally caps the eligible source aspect.
- Omitting either enabled option's limit permits that option for any trusted
  content in its direction.
- No crop is manufactured from provisional, stale, or contradictory evidence;
  subtitle/overlay safety remains intact. A current trusted full raster is
  explicitly valid source geometry for a selected fill operation.
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
