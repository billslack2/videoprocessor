# VP-0172: Source-driven output display-mode selection

## Status

In Progress (2026-10-21; reviewed software merged with VP-0200;
projector-on qualification pending). [PR #184](https://github.com/billslack2/videoprocessor/pull/184)
is merged and pushed to `v1.4.02-beta` at
`acb0cf6f4c5e9bce045cade13e7822b6f4f2a810`, with verified parents
`71043e192b658c433dc8d888632f4740d1e61b01` and tested combined head
`921791d0768a18df4087cbc09a97110bd609aefc`. The remote merge tree exactly
matches that tested head. GitHub marks #181 merged; original fork #183 remains
open, with its commits and upstream review changes already included in #184.

The reviewed #181 layer `3a8f8a08` contains twelve backend/cadence/DPI regressions
and passed all 2,948 native tests plus four focused Config checks. The combined
head adds fourteen OSD cache/font/bitmap tests and passed 378 targeted regressions
plus the same four Config checks. All 26 new tests passed on the final head.
Both clean x64 Release solution builds succeeded; no failures/skips in those
test selections. The complete native suite was not rerun on the final head,
and no tests were rerun for this unchanged-tree merge.

The merged stack addresses stale raster cache lifetime, swapchain size
publication and native-panel clipping while keeping OSD polling free of new
display-driver queries. Routing and consecutive read-back verification use
shared production helpers exercised by tests. Source history preserves both
original PRs. No deployment or active config/state edit was performed for this
merge. See the [review evidence](../assets/VP-0200/pr181-pr183-review-20261021.md)
for exact pins, merge verification and remaining physical-display acceptance.

## Successful tester run (2026-10-21)

Analysis of the newly supplied `vp-log-pr181-2026-10-21.zip.txt` confirms six
first-candidate GDI 1080p59.94 selections with 1920x1080 client and swapchain on
every selection; five successful CCD 2160p23.976 requests; six verified GDI
restores to 4K23.976; and no mode unverified/rejected/rollback/failure messages.
1080p candidate-to-verification intervals are 0–1 seconds at one-second log
precision. Eligible lists put 29.97 before arbitrary 50/30 Hz fallback, but no
fallback or failure recovery was exercised because first candidates succeeded.

The tester reports the same RTX 3060 Ti / 511.17 rig at 200% scaling, with
#181 `4fd790ce` on `88289699` plus their open PRs. Both modules actually log
clean combined build `07ab5fffc593fdb77e2d07a38786d33e193f4a60`.
The projector was off and TightVNC was used: mode read-back and geometry are
qualified for this run; projected picture, independent receiver timing and
visually checked native OSD/1:1 mapping are not. This does not qualify the new
beta `71043e19` combination. No new local tests, source changes, merge,
deployment or config edits were performed.

[Detailed evidence and remaining acceptance](../assets/VP-0200/pr181-tester-validation-20261021.md).
The #181 description now records this partial qualification. The announced
[OSD raster PR #183](https://github.com/billslack2/videoprocessor/pull/183)
is open and stacked on #181 at `4fd790ce`; review/integration is separate.

## User story

As a VP Renderer operator, I want to supply an ordered list of permitted
output display modes and have VP select one for each source cadence, so I can
keep unsupported raster/rate combinations off the wire and explicitly choose
1:1 DCI output when my display path supports it.

## Product contract

The operator, not VP, owns raster policy. Configuration supplies one ordered
list of complete permitted output modes. VP uses its existing two-pass
rational-rate policy to determine eligibility and chooses the earliest listed
eligible mode. It must not infer a mode from EDID, link bandwidth, observed
failures, or source raster.

Long form `WIDTHxHEIGHTpRATE` is canonical and always accepted. The existing
short form `HEIGHTpRATE` resolves to the documented 16:9 width for that
height; it never depends on source raster or enumerated modes. Consequently,
`2160p24` means `3840x2160p24`, never 4096x2160. DCI modes have no short form,
and all diagnostics print long form. The stored rate remains an exact rational
(`23` = 24000/1001, `59` = 60000/1001, and so on); interlaced entries are
rejected.

With no configured list, behavior remains VP-0167 behavior exactly: refresh
may change, desktop raster does not.

## Scope

1. Add a renderer/output configuration field, editor control, validation, help,
   and diagnostics for an ordered list such as
   `3840x2160p23 4096x2160p24 1920x1080p59`.
2. Extract a platform-independent, unit-testable display-mode value and parser;
   compose it with VP-0167's exact/tight/bounded-fallback rational ranking
   rather than duplicate that policy.
3. Enumerate progressive 32-bpp modes from both DXGI and GDI, union and dedupe
   by `(width, height, rational rate)`, and log whether each mode came from
   DXGI, GDI, or both. Do not discard modes merely because their raster differs
   from the current desktop.
4. Carry the selected width, height, and rational rate through the existing
   display transaction. Prefer one enumerated progressive GDI fullscreen mode
   request for raster changes. Retain verified CCD for refresh-only changes and
   custom timings without a GDI selector; preserve virtual-mode index handling.
5. Verify the applied width, height, and refresh after the transition. Candidate
   rollback and retirement restore must restore the entire pre-VP display mode,
   not only the rate.
6. Emit source raster/cadence, enumerated and eligible modes, selected long-form
   mode, selection path, and named skip/failure reasons. If a configured mode
   is not enumerated, print it and the enumerated set. Once per source change,
   explain when a DCI source was not selected because no full `4096x2160` entry
   was configured.

## Non-goals

- HDR/color transport, monitor topology, target-only display-session policy,
  scaler policy, viewport/aspect policy, or per-raster calibration.
- Creating or requesting custom display timings, EDID/link-bandwidth inference,
  or automatic source-raster matching.
- Routing mode selection through legacy refresh commands or event actions.

## Acceptance criteria

- Parser tests prove canonical long forms, fixed 16:9 short-form resolution,
  malformed/interlaced rejection, and distinct UHD/DCI modes at the same rate.
  `2160p24` resolves to 3840x2160 even when the source and enumerated modes are
  DCI.
- Rate matching preserves VP-0167 exact, tight, and bounded-fallback behavior;
  at one cadence, the earliest configured eligible raster wins.
- With `3840x2160p23 3840x2160p24 1920x1080p59 1920x1080p60`, 23.976 and
  59.94 sources select 3840x2160p23 and 1920x1080p59 respectively, the wire
  raster matches, one display transition occurs per source change, and the
  pre-VP full mode is restored on retirement.
- With an explicit DCI-first list and an enumerated 4096x2160p24 mode, a DCI
  source selects that mode and is presented 1:1 without horizontal resampling
  or 16:9 pillarboxing. A 16:9 source at the same cadence follows list order
  and retains aspect-preserved fitting.
- A listed but unavailable DCI mode produces the named diagnostic and playback
  continues at the next eligible listed mode.
- With no list, existing refresh-only behavior is unchanged. Embedded-child
  presentation still skips display-global mode changes.
- Focused tests and x64 Release builds pass; bench validation verifies the
  applied wire raster, one transition per source change, and restore behavior.

## Dependencies and readiness

- VP-0167 owns the existing rational candidate ranking, verification, and
  rollback behavior. Its tracker state is Review; confirm its acceptance
  coverage before extending the existing transition policy.
- Before implementation, verify the current configuration namespace and exact
  `SetDisplayConfig` mode-info ownership on the current beta integration base.
- The DCI bench case requires the target driver to enumerate the requested DCI
  timing; VP must not create it.

## Related configuration story

2026-10-18: [VP-0200](VP-0200_profile-based-resolution-refresh-configuration.md)
owns the dedicated Display modes page, profiles, Auto/Advanced choices, migration,
and saved mockups. VP-0172 retains the underlying mode-selection and transition
contract. Coordinate one configuration model and one runtime controller; the UI
work is tracked separately rather than implemented twice. Auto explicitly selects
existing refresh-only behavior; an empty Advanced list is invalid in the new UI.
