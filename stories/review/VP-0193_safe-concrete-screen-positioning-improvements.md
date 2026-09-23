# VP-0193: Safe, concrete screen positioning improvements

## Status

Review (2026-09-23). Implementation committed and pushed as
`e0ce8e8a5ab513ad1d021b9b9fed38493dd3308b` on
`codex/vp-0193-screen-positioning` in
`E:\codex\videoprocessor\vp-0193-screen-positioning`.
Draft PR: https://github.com/billslack2/videoprocessor/pull/111.
Current default/latest beta was rediscovered as `v1.3.005-beta`, still at
`8becfd68a1832b4f615657b3e67398661a7dde40` when the PR was created.

The source-selection -> size/mapping -> destination-placement model is sound.
The correction uses a shared final-placement helper after picture sizing in
linear, active NLS and fallback NLS paths. It preserves existing outer-screen
inset first, then spends only the remaining request inside the screen. Each
boundary is limited to its whole-pixel slack. Source selection and picture
sizing are absent from the helper interface. Center, zero padding and no-space
behavior remain unchanged. Final-layout logs now report outer/inner slack and
inset, effective/requested padding, limiting reason and all three rectangles.
Configuration help and reference text describe the corrected behavior.

Automated evidence (paths relative to the source worktree):

- Red regression: `TestResults/vp0193-red.trx` records expected 50 / actual 0
  with the original outer-only calculation before the correction.
- All eight added native regressions pass, including 1,800 geometry combinations,
  nested containment, no-space, invalid/whole-pixel cases, selected subtitle
  envelope entry/hold/release and profile inheritance/persistence/reload.
- Full native suite: `TestResults/vp0193-native-all.trx` records 1,473/1,474
  passed. The single failure explicitly required a Release renderer DLL that
  had not yet been built. After building it, the integration test passed in
  `TestResults/vp0193-renderer-integration.trx`. No unresolved failures remain.
- Qt tests `every page round trips` and `Screen Config sections and inline units`
  pass (`vp0193-config-roundtrip.log`, `vp0193-config-screen.log`). They cover
  output-pixel units, Center disable/value retention, saved padding and reload.
- x64 Release builds succeeded for host, renderer, ConfigDiscovery, Config and
  tests. Native projects used installed VS2019 v142/MFC; Qt projects used VS2026
  v143 and Qt 6.8.3. Production build logs: `vp0193-release-build.log` and
  `vp0193-config-release-build.log`. Host/renderer version generation records
  source commit `e0ce8e8a` and `VERSION_DIRTY=false`.
- Existing compiler warnings and Qt deployment-tool warnings (DX compiler DLLs
  and VCINSTALLDIR) remain; this is build evidence, not installer qualification.
  Initial toolset/debug-info build issues were resolved with the installed
  native MFC toolset and a clean native-test rebuild.

Release output SHA-256:

- `x64/Release/VideoProcessor-GUI.exe`:
  `70C9B4249474E3F2A2E7F8BD9F543E539570DCED37FCCF381B1972C33EB0AB0F`
- `x64/Release/vprenderer/VideoProcessorVPRenderer.dll`:
  `C613E6356F1E352A670835AD221BD5747FF40DD2E0FD187D43D41943EF9FE8A8`
- `x64/Release/VideoProcessorConfig.exe`:
  `CC0DBB18F14090971732F658E8DD2F6D9623D6F4AFBD6EA33CC7C2EFE8C8BF41`

Remaining acceptance: review placement ordering/compatibility and visually
validate Top/Center/Bottom with zero/positive/excessive padding on 16:9 and scope
targets, including subtitles, NLS, Screen/Zoom selection and live Apply. Automated
geometry/envelope and reload coverage does not establish actual display/capture
behavior. Existing crop/profile authority policy is unchanged; VP-0190 remains
separate. The other bottom-positioning/zoom-to-fill field report is not claimed
resolved. No deployed log was available at either documented path on inspection.

Proposed merge/release decision: keep in Review until code/build review and live
visual acceptance are complete. No merge, deployment, active configuration edit,
installer or portable package was performed.

## Requested build and deployment (2026-09-23)

The user requested an EXE, setup and deployment. Built the exact clean, pushed
feature commit `e0ce8e8a5ab513ad1d021b9b9fed38493dd3308b`; PR #111 remains
unmerged. Latest beta/default was rechecked as `v1.3.005-beta` at `8becfd68`.
The previously deployed gradual-expansion commit `de30461a` is an ancestor of
that beta, so its source changes are included.

Export directory:
`E:\codex\releases\VideoProcessor-1.3.005-beta-e0ce8e8a5ab5-20260923`.

- `VideoProcessor.exe` SHA-256:
  `A2766757AFAC247298C13793313BB0D333476825EA5C945E469393F447B1C802`
- `VideoProcessorSetup-1.3.005-beta-e0ce8e8a5ab5.exe` SHA-256:
  `923C94CDCE73A7C68FB510AC35CFE0F562498E4BBD6116F1664EB30FC24530DD`
- Rebuilt renderer SHA-256:
  `8DAB4BF3E4A988DD258C69E44794475A02622173D578D3D19D79409F021B851B`
- Full clean x64 Release solution rebuild passed. All 1,474 native tests passed
  in this build; installer support, identity and runtime-packaging checks passed.
  Both targeted configuration tests passed again against the rebuilt binaries.
- Exact tools, source fingerprint, build/payload hashes and test results are in
  `validation/release-receipt.json`, `installer-build.json`,
  `INSTALL-MANIFEST.json`, checksum sidecars and the validation logs.
- Setup is unsigned and includes app-local runtime DLLs. No public release or
  clean-Windows/interactive qualification is claimed.

Deployment is pending: isolated installer lifecycle testing cancelled safely
because existing Config processes were running: PID 10840 in
`C:\Videoprocessor\vp` and PID 24308 in `C:\vptest2`. Both predate this task's
build. The user was asked to save edits and Exit both tray instances; they were
not terminated. Setup's safety check ran before replacement, and no deployed
files/configuration were changed. Resume lifecycle/failure tests after they exit,
then run the prepared verified-backup deployment and record final installed
host/renderer hashes and unchanged operator configuration/state. Remaining live
screen acceptance still applies. Story remains Review.

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

Readiness completed before implementation: existing settings and live-apply
plumbing are usable, source/size/placement separation is appropriate, and the
padding defect reproduces using fixed geometry. Review draft PR #111 and the
recorded automated/build evidence. The next acceptance step is live visual
validation listed under Status, followed by the merge/release decision.

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
