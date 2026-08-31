# VP-0154: Automatic black-bar crop and aspect-limit fill

## Status

Done (2026-08-31). Implementation was completed on
`codex/vp-0154-black-bar-crop-limit` at `383762df`, rebased on current
`v1.3.001-beta` tip `85abed0`. Config now has two independent controls, both
off by default: **Crop narrower content to fill screen** and **Crop wider
content to fill screen**. Each has its own optional Aspect ratio limit and
preserves the literal value entered (for example `2.20:1`). A blank narrower
limit explicitly allows any trusted narrower content; a blank wider limit
explicitly allows any trusted wider content, including in a named Screen Config
that would otherwise inherit a root-profile limit. A narrower limit is the minimum eligible aspect, while
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

The first tested integration containing VP-0154 was `v1.3.002-beta` at
`3d23a55b`; that branch became the repository default on 2026-08-28 and the
story remained in Review at that stage pending operator acceptance.

The latest review commits also make newly entered profile overrides immediately
use the normal bright field colour, label the profile currently being edited as
active or not active, and apply Screen Config profile edits through the live
profile path used by F2/F3 rather than restarting the renderer. The new Config
layout adds compact VP Renderer tabs: **Rendering**, **Scaling**, **Color**,
**Output**, **Screen**, **Zoom**, and **Processing**. Scaling has independent
ordered profiles for upscaler, downscaler, and anti-ringing; those selections
apply through the same live profile path. Screen and Zoom are shared views of
the same selected screen profile: Screen owns geometry, while Zoom owns
crop/fill and subtitle placement. Rendering now retains only Debanding,
Dithering, and Display bit depth under **Processing**. The x64 Release Config
build and focused GUI/native profile-policy regressions passed. The verified
x64 Release Config executable was deployed to `C:\Videoprocessor\vp\config`
on 2026-08-27; its installed hash matched the build and the active
`VideoProcessor.cfg` remained unchanged. This intermediate review build was
validated before final integration.

Follow-up `036bfb43` makes the two optional limit values fail open: blank,
legacy, malformed, and out-of-range values are treated as no limit rather than
rejecting the unified renderer configuration. The editor no longer writes an
internal `none` sentinel for a cleared field and removes legacy/malformed
optional values when it next saves the document. The four invalid `none` lines
previously written into the operator's active configuration were removed after
backing it up at
`C:\Videoprocessor\vp\backups\vp-0154-invalid-none-20260827-1501`.
The tested x64 Release host from this follow-up was deployed to
`C:\Videoprocessor\vp\VideoProcessor.exe` (SHA-256
`EA63FBE1E18451AD909D49D956341C91C4E9D65C748EBFCA115A44E7F3E46909`).

Final follow-up `b5bcea95` completes independent Screen, Zoom, Scaling, Color,
and Processing live profile application without resetting the live queue,
removes unsupported pathological downscaler choices, labels the explicit
upscaler bypass as **Use GPU**, and migrates legacy downscaler values to Auto.
VP-0154 was merged into `v1.3.001-beta` at merge commit `bdcb60cf`. A clean x64
Release build completed, Config tests passed, and the native suite passed
940/941 with only the pre-existing configuration-document inventory mismatch.
The canonical 57-file release manifest produced and verified
`VideoProcessor-v1.3.001-beta.zip`; it contains only
`VideoProcessor.cfg.example`, never the operator's active configuration.

Operator-acceptance follow-ups on `v1.3.004-beta` add the fixed crop aspect
control and complete its runtime contract. Commits `a7fcb416`, `82fe5fe7`,
`79bcc2d4`, `3a81224b`, `9784af22`, and `5e20e71d` add the UI/profile setting,
fixed source-aspect crop, black matte and visible-picture OSD placement, and
atomic transition/reset state. Commit `5d266d6a` keeps the fixed crop
authoritative through detector timeouts and NLS ACTIVE/SAFE_FIT geometry while
preserving final-visible-picture HDR analysis. Final acceptance commit
`2ecc3d39` separates fixed aspect from automatic crop: menu/subtitle geometry
may reposition the fixed view only when automatic crop or **Keep subtitles
inside screen bounds** is explicitly enabled. With both controls off, the
fixed rectangle remains anchored.

`v1.3.004-beta` was fast-forwarded to `2ecc3d39` and verified as the exact
remote integration tip. The final x64 Release renderer build passed all seven
focused transition regressions and all 206 broader crop, HDR-analysis,
render-parameter, and NLS tests. It was deployed to
`C:\Videoprocessor\vp\vprenderer\VideoProcessorVPRenderer.dll` with SHA-256
`EE0DD57899670A603966784905DD096BB96F6FC94CF875945D4D0FB05DA8794D`;
the prior renderer is recoverable from
`C:\Videoprocessor\vp\deployment-backups\vp0163-fixed-subtitle-gate-20260831-102835`.
The operator confirmed the final behavior looked correct, completing
acceptance.

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
