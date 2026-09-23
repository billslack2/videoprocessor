# VP-0193: Safe, concrete screen positioning improvements

## Status

In Progress (2026-09-23). Implementation authorized in this task. Current GitHub
default and latest beta both resolve to `v1.3.005-beta`, fetched at
`8becfd68a1832b4f615657b3e67398661a7dde40`. Source branch:
`codex/vp-0193-screen-positioning`; clean starting worktree:
`E:\codex\videoprocessor\vp-0193-screen-positioning`.

Readiness review: the existing source-selection -> size/mapping -> destination
placement model is sound. Configuration keys and live-apply plumbing already
exist. The confirmed correction is final-picture padding; no new detector,
source-lifetime, crop, or subtitle policy is required. Keep the existing outer
screen inset first for compatibility, and use only the unconsumed request in
eligible inner picture space after fitting. Apply this once through a shared
final placement helper used by linear and active/fallback NLS paths. Whole-pixel
insets are bounded at each containment boundary; screen remains inside output,
and picture remains inside screen. The helper receives destination rectangles
only, so cannot change source selection or scale.

First validation step: extract the existing placement behavior and add a native
regression for inner-only vertical space; record its failure before correcting
it. Follow with nested-layout, no-space, invalid-geometry, and presentation
sequence coverage, configuration round trips, and an x64 Release build. Hardware
visual acceptance remains a separate required review item. No deployed debug
log was present at either documented log path during readiness inspection.

## User story

As a VP Renderer operator, I want Top, Center, Bottom, and screen-edge padding
to place the final fitted picture predictably, so I can align it to my screen
or masking without unintended crop, scaling, subtitle loss, or movement.

## Background and evidence

The inspected repository-of-record beta was `v1.3.005-beta` at
`8becfd68a1832b4f615657b3e67398661a7dde40`. This is investigation evidence,
not a pinned integration base for future implementation. Rediscover and fetch
the latest beta before source work.

The current layout fits the configured screen inside the output, applies
`screen_edge_padding` using that outer fit's vertical slack, then performs the
final linear picture fit. Active NLS returns after its screen fit. As a result,
a configured screen that fills the output can consume no outer slack while the
final fitted picture still has unused vertical space. Padding is then reported
as zero and has no effect, despite space available around the picture.

Concrete reproduction target: a 1920x1080 output and 16:9 screen with a 2.40:1
source picture. The fitted picture is 1920x800 and has 280 vertical pixels of
available space. With Top and 50 px padding, the intended picture rectangle is
(0,50)-(1920,850). Bottom with 50 px gives (0,230)-(1920,1030). The current
outer-screen-only padding calculation cannot use this space.

Recent black-bar improvements should help placement by stabilizing the source
bounds supplied to layout. Their positioning benefit has not been measured.
They do not correct the padding order above. VP-0189 also recorded the separate
bottom-positioning/zoom-to-fill report as outside that work; do not assume this
new story's padding correction explains every field report.

## Scope and required behavior

1. Reproduce the padding-order issue with fixed, already-known source geometry.
   Correct padding placement using the completed picture dimensions and the
   eligible vertical space. The regression must not require changing the
   detector or its trust thresholds.
2. Give output bounds, configured screen bounds, and final picture bounds
   explicit roles. Preserve established placement of a scope screen inside the
   output. Handle outer-screen slack, inner-picture slack, and both together;
   consume a requested inset only once. Any use of inner slack must respect the
   configured screen, and any movement of the screen must respect the output.
   Masked or otherwise unavailable space is not eligible picture space.
3. Retain the existing settings and units: `vertical_alignment` is Top, Center,
   or Bottom; `screen_edge_padding` is a non-negative count of output pixels.
   Preserve existing successful outer-only padding cases. Center ignores the
   stored padding. Zero or omitted padding preserves existing alignment.
4. Clamp padding to the eligible space. When there is no vertical space,
   placement remains unchanged and the effective inset is zero. Never create
   space by scaling, stretching, source cropping, or clipping the picture.
5. Apply the same placement contract to linear rendering, active NLS, NLS
   passthrough/safe-fit/waiting, and anamorphic compensation, using the final
   dimensions selected by each existing mapping. Keep intentional differences
   in their sizing behavior. A common final placement calculation is preferable
   if it removes divergent behavior without broad renderer restructuring.
6. Preserve the existing crop, zoom, NLS, and subtitle visibility decisions.
   Compute placement from the selected presentation envelope. Respect required
   subtitle accommodations and return to the configured resting alignment when
   they expire. Padding must not hide pixels that the existing presentation
   policy selected for visibility or turn temporary subtitle adjustments into
   a new content aspect. Real source or presentation-size changes may move the
   picture; do not hide them behind a new hold or smoothing policy.
7. Keep positioning deterministic through Screen/Zoom profile changes and live
   Apply. Identical geometry and settings produce identical placement. Do not
   reset active-picture authority solely because placement changes.
8. Extend existing final-layout diagnostics to show requested/effective padding,
   eligible vertical space, output/screen/picture rectangles, selected alignment,
   and a concise reason when padding is ignored or limited. Reuse bounded or
   change-triggered logging. Update help/reference text so users can understand
   the no-space case; a new OSD panel or configuration page is not required.

## Acceptance criteria

1. A failing regression demonstrates the 1920x1080 / 2.40:1 example before the
   correction. Afterward, Top/Bottom at 50 px match the rectangles above. Center
   remains (0,140)-(1920,940), including when a padding value is saved.
2. Zero, omitted, positive, and excessive padding are covered. For that same
   280 px slack example, an excessive request clamps to 280 px without changing
   picture dimensions or source bounds. Effective padding and limiting reason
   agree with the final destination rectangle.
3. Equal-aspect and pillarboxed cases with no eligible vertical slack have no
   vertical displacement. Configured scope-screen cases cover outer slack only,
   inner slack only, and both, with no double padding or movement into excluded
   screen regions. Previously valid outer-only layouts remain compatible.
4. Tests exercise the actual renderer layout composition or a shared helper used
   by every relevant renderer path, including active NLS's early-return path.
   Tests of the standalone FitAspect function alone are insufficient.
5. For each placement-only change, assert unchanged source crop, picture width
   and height, scaling/aspect mapping, and crop/NLS authority. Exercise automatic
   crop off with known geometry, trusted automatic crop, and safe fallback to
   the full raster. Fallback uses its actual dimensions; it does not invent
   active-picture bounds to satisfy a requested alignment.
6. Sequence coverage includes subtitle entry/hold/release, genuine aspect
   changes, and Screen/Zoom profile changes. Verify required pixels stay visible,
   padding is applied once, and returning geometry restores its prior placement.
   Do not require an invariant position when the presentation dimensions change.
7. Verify default/inherited settings, Center's disabled padding field with value
   retention, persistence, and live Apply through the existing configuration
   path. No active user configuration is rewritten for this correction.
8. Relevant native/configuration tests and an x64 Release build pass. Before
   marking Done, record visual validation of Top/Center/Bottom with padding on a
   16:9 target and a configured scope target, including subtitles and NLS where
   available. Distinguish automated evidence from remaining live validation.

## Readiness and next action

Reproduce the inner-slack example on the freshly discovered beta. Record the
coordinate/containment contract for nested screen and picture placement, and
verify the current settings/live-apply and subtitle paths before implementation.
Compare existing outer-only layouts to the proposed final layout so the change
cannot silently reposition established screen calibrations. Keep any additional
fix limited to a demonstrated layout defect with a regression. If a new unknown
would change crop authority or subtitle policy, record it separately rather than
enlarging this story's correction.

## Related stories and source locations

- VP-0155: configured-screen alignment, including active NLS; preserve its fix.
- VP-0186: existing screen-edge padding contract and configuration control.
- VP-0189: crop stability improvements; consume their results unchanged.
- VP-0190: NLS-only profile authority retention; coordinate any overlap without
  absorbing that independent story.
- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`: screen fit and
  padding near lines 11957-11986, and final fits/early return near 12202-12238 in
  the inspected commit.
- `src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.cpp`: FitAspect.
- `src/VideoProcessor-Test/AlphaSourceCropPolicyTests.cpp`: existing fit tests.
- `src/VideoProcessor-Config/ConfigEditorWindow.cpp`, configuration tests, and
  `CONFIGURATION.html`: settings behavior and user-facing explanation.

## Out of scope

Detector/AR threshold changes, new manual source-crop or aspect overrides,
unbounded pan/scan, arbitrary offsets, horizontal positioning, new scaling or
zoom modes, placement smoothing, subtitle detection/relocation redesign, and
madVR placement control. madVR continues to own its native screen/zoom settings.
Implementation, merge, packaging, and deployment are not performed by creating
this backlog story; deployment requires a separate user request.
