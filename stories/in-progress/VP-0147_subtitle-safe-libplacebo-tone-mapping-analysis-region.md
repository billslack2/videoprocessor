# VP-0147: Subtitle-safe libplacebo tone-mapping analysis region

## Status

In Progress (2026-08-28). The local proof is being implemented on branch
`codex/vp0147-analysis-roi` in worktree
`C:\Videoprocessor\vp\vprenderer\.codex-worktrees\vp0147-analysis-roi-0cf07a9`,
based on authoritative `origin/v1.3.003-beta` commit `0cf07a9`.

The architecture review reached a conditional **GO**. A separate analysis
region is technically viable, but a simple same-frame RGB hook is not: the
libplacebo high-level renderer owns the tone-map detector state privately and
later color mapping consumes the original frame metadata. The selected first
proof is therefore a local libplacebo fork that adds a generic same-frame peak
analysis rectangle to the high-level renderer while preserving the complete
presentation. The user explicitly prohibited submitting, pushing, or opening
an issue/MR/PR against remote libplacebo while this work is in progress.

The fork and VP integration remain local proof artifacts. VP may expose the
proof through an opt-in, default-off Zoom / Subtitles setting, but production
shipping and any upstream submission remain separate reviewed decisions.

Do not ship a cached-metadata or subtitle-duration freeze as an interim
solution.

## Local proof checkpoint (2026-08-28)

Local libplacebo 7.360.1 branch `codex/vp0147-analysis-roi-v7`, commit
`c3a3d203`, now provides the API-360 analysis crop. Its no-push URL remains
`no-push://libplacebo-local-only`; nothing was submitted upstream. The x64
Release DLL hash is `D2BCC6E62DF86760825639949448594D69024C0C2544D0DFC3D6C58D05E23507`.
On all three D3D11 adapters the controlled full max/average PQ
`0.565607/0.419726` changed to ROI `0.544772/0.388818`, matching the
independent ROI reference `0.544777/0.387234`.

VP commit `b08251b` adds the default-off Zoom / Subtitles toggle, fail-open
trusted-geometry policy, policy-transition evidence, and five-second aggregate
ROI/metadata metrics. The full x64 Release solution builds; ROI policy tests
pass 4/4, libplacebo parameter/ABI tests pass 11/11, and the offscreen Config
suite passes. The full VP suite passes 954/955, with only the beta tip's
pre-existing HTML inventory mismatch for four crop-to-fill fields failing.

At the user's direction, the exact committed x64 Release package was deployed
locally to `C:\Videoprocessor\vp` for live testing. The pre-deployment files
and an unchanged safety copy of `VideoProcessor.cfg` are under
`deployment-backups\vp0147-20260828-125543`. All 57 staged immutable files
match the deployed tree, the active configuration hash remained unchanged, and
the deployed VP Renderer plugin passed a `LoadLibraryEx` smoke test with API 14
and all required exports. The toggle remains omitted/default-off until the user
enables it in Config.

After the later VP-0161 render-health deployment exposed that its beta-only
executable did not recognize VP-0147's active
`hdr_peak_analysis_picture_only` setting, the user requested a combined local
build. VP-0161 was applied without conflicts to the exact local VP-0147 tip
`f99541f8` on local-only branch `codex/vp0147-vp0161-render-health`, producing
commit `651ec669`. The clean x64 Release solution build passed; 29 focused
ROI/parameter/plugin/health tests passed, the offscreen Config suite passed,
and the complete native suite passed 965/966 with only the same pre-existing
HTML inventory mismatch failing.

That combined build now supersedes the earlier local deployment. Its backup is
`C:\Videoprocessor\vp\backup-before-vp0147-vp0161-20260828-181044`.
The deployed executable SHA-256 is
`7FE58AE4F3531ACCD6E34B848FBBC3193BC05073C0F8222C28C64504CA2825CB`,
the renderer DLL SHA-256 is
`38B6C7F439894EF15F5A54EF323788BCA35BC359E153FE69D3DF2684E8DD2D10`,
and the matching local libplacebo fork remains
`D2BCC6E62DF86760825639949448594D69024C0C2544D0DFC3D6C58D05E23507`.
The active configuration was not edited and remains SHA-256
`DAC456C0D12657A2B4D10569D79D5B4DDCF7FB0816D8C6F70F53965E8FFBD7C3`.
The VP-0147 fork and combined branch remain local and were not pushed.

This test deployment is not yet the spike's final GO. Live subtitle A/B, the
input/refresh/NLS timing matrix, reset/scene/dynamic-metadata coverage, and
queue/presentation measurements remain. Keep the story In Progress until that
evidence is recorded.

## User story

As a VP Renderer user watching HDR material with burned-in subtitles in black
bars, I want subtitle appearance and disappearance not to dim or brighten the
underlying picture, while retaining libplacebo's adaptive tone mapping for the
actual visible picture content.

## Confirmed problem

VP Renderer deliberately maintains two related concepts: trusted logical active
picture geometry and the final source rectangle presented to the display. Bar
subtitle handling may expand or translate the latter so the burned-in glyphs
remain visible. The final `pl_frame.crop` is then passed to `pl_render_image`.
Libplacebo 7.360.1 exposes peak percentile, smoothing, black cutoff, and
enable/disable controls, but its high-level renderer has no separate ROI or mask
for `pl_shader_detect_peak`; peak detection operates on the current rendered
image. Bright subtitle pixels can therefore alter both detected peak and frame
average and change the tone-mapping curve for the whole picture.

mpv's `gpu-next` renderer inherits the same libplacebo boundary. Separate
player-rendered subtitles can be overlays, but burned-in pixels receive no
special ROI treatment; mpv issue 6368 records the same subtitle-triggered
brightness symptom. MPC Video Renderer avoids this particular pumping by using
a fixed per-pixel Hable HDR-to-SDR curve rather than adaptive histogram
analysis; it is not precedent for a subtitle-aware dynamic detector.

## Architecture decision

Use these implementations in priority order:

1. For the local proof, carry a minimal, generic libplacebo fork that adds an
   optional peak-analysis rectangle independent of `pl_frame.crop`. Keep the
   existing private same-frame detector, smoothing, scene response, dynamic
   metadata precedence, and complete rendered presentation. Build and identify
   the fork separately from stock libplacebo, keep its source and exact commit
   reproducible, and do not contact or push to remote libplacebo without a new
   explicit user instruction.
2. If the local same-frame rectangle proves untenable, use a VP-owned detector
   state through
   public libplacebo shader and detected-metadata APIs, pipelined by exactly one
   frame:
   - before rendering frame N, retrieve the ROI result dispatched for frame
     N-1;
   - inject its CIE-Y `max_pq_y` and `avg_pq_y` into frame N only when the
     result, source generation, geometry authority, and metadata policy all
     remain valid;
   - at `PL_HOOK_RGB`, after native RGB or YUV ingress has been decoded to RGB,
     sample the resolved source ROI, call `pl_shader_detect_peak` with a
     VP-owned state, and dispatch the compute shader for frame N;
   - return no replacement image from the analysis hook so the complete
     presentation remains pixel-identical;
   - compose a stable hook list with analysis before NLS rather than replacing
     the existing NLS hook pointer.

This is continuously refreshed per-frame analysis, not a subtitle-duration
metadata freeze. Start the spike with deterministic N-1 retrieval; an older or
opportunistically polled result is not acceptable merely to avoid measuring a
readback stall.

A custom tone mapper, a VP-side mask-and-restore presentation chain, and a
second full high-level render are rejected as the initial production
architecture. They
duplicate or take ownership of substantially more of libplacebo's scaling and
color pipeline and may be reconsidered only through a new reviewed decision if
both preferred paths fail.

## Mandatory non-shipping spike

The first implementation increment is an isolated technical spike. It must not
deploy new binaries, change active user configuration, enable the feature by
default, or be treated as partial acceptance of this story. It may add the
requested default-off diagnostic configuration and UI control needed for local
A/B testing.

The spike must provide reproducible evidence for all of the following:

1. The forked high-level renderer accepts an optional analysis rectangle,
   excludes every outside pixel from peak, average, histogram, and active-count
   statistics, and renders output identical to the stock full-presentation
   path.
2. The rectangle remains stable when subtitle arbitration expands or translates
   presentation geometry, intersects final visible geometry when required, and
   coexists with NLS without subtitle-triggered shader or renderer rebuilds.
3. Same-frame private detector state supplies both `max_pq_y` and `avg_pq_y`
   without a VP-owned metadata readback or one-frame injection path. Measure
   CPU wait, GPU duration, queue behavior, and presentation latency.
4. Existing libplacebo scene response remains same-frame; source changes,
   seeks, renderer resets, and trusted analysis-region generation changes reset
   detector state deterministically without flushing compiled shader programs.
5. Source changes, seeks, renderer resets, geometry-generation changes, GPU
   analysis failure, and missing results select a documented full-frame/static
   fail-open path without reusing invalid ROI metadata.
6. Authoritative DV, HDR10+, or equivalent dynamic metadata bypasses derived
   CIE-Y metadata rather than being merged with it. At minimum this requires a
   pure synthetic-metadata policy test; end-to-end source validation additionally
   depends on VP carrying such metadata to the renderer.
7. Native RGB and P010/P210 inputs converge on equivalent decoded-RGB analysis
   within a documented tolerance, and SDR never enters the ROI detector.
8. A 4K23.976/24/50/59.94/60 comparison records stock full-frame detection,
   peak detection disabled, ROI analysis without NLS, and ROI analysis with
   NLS. Include intermediate-texture bandwidth, compute time, CPU readback
   time, shader-cache evidence, queue depth, dropped/repeated frames, and
   presentation latency.
9. Bounded diagnostics prove the selected path without per-frame log spam:
   feature toggle and fork/API identity, source generation, trusted picture,
   presentation crop, effective analysis rectangle, rectangle/fallback reason,
   metadata source, detected `max_pq_y`/`avg_pq_y`, scene/reset state, and
   aggregated timing/counter evidence.
10. The configuration editor exposes a default-off control under VP Renderer /
    Zoom / Subtitles, persists it through the canonical profile schema, applies
    it through the established live-settings path when safe, and truthfully
    reports any restart requirement.

The spike ends with one explicit decision:

- **GO**: select the local same-frame ROI or VP-owned N-1 architecture, record measured
  tolerances and cost budgets in this story, remove or hard-disable experimental
  scaffolding, and proceed with the production acceptance criteria.
- **NO-GO**: retain current full-frame/static behavior and record the blocking
  result. No-go includes inability to isolate and reproduce the fork, inability
  to preserve presentation pixels, stale state across source/region changes,
  unbounded synchronization, subtitle-triggered shader/renderer rebuilds, or
  exceeding an agreed 4K frame-time/latency budget.

## Required behavior

1. Separate tone-mapping analysis geometry from presentation geometry. The
   full final presentation crop, including accepted burned-in subtitle pixels,
   must still be rendered unchanged.
2. When current trusted active-picture authority exists, derive the analysis
   region from the intersection of the trusted logical active picture and the
   final visible source crop. This excludes revealed black-bar content while
   avoiding influence from picture pixels removed by NLS or presentation crop.
3. Use the same trusted logical analysis basis continuously while its geometry
   remains authoritative; do not switch histogram scope merely because subtitle
   detection engages or releases. Resolve the per-frame ROI as its intersection
   with the final visible source crop. If presentation translation or NLS
   actually changes that visible intersection, the ROI may change only by that
   geometric necessity. Full-raster material should naturally retain
   full-raster analysis.
4. Feed both peak and average-luminance statistics from the analysis region into
   libplacebo tone mapping. A percentile-only workaround is insufficient because
   it does not remove subtitle influence from frame-average statistics.
5. Preserve valid source dynamic metadata precedence. Do not replace usable
   Dolby Vision, HDR10+, or other authoritative per-scene metadata with
   VP-derived measurements.
6. Fail open when analysis geometry or GPU analysis is unavailable: preserve
   the picture and subtitle, use the documented existing full-frame/static
   libplacebo behavior, and emit a bounded diagnostic. Never blank, clip, or
   silently discard subtitle-bearing regions to obtain cleaner statistics.
7. Maintain temporal detector continuity without carrying measurements across
   a source generation, seek, reset, or authoritative analysis-region change.
   The selected same-frame fork must preserve libplacebo's current scene-change
   response. If the delayed fallback is used instead, it must reset on an
   immediate conservative hard-cut candidate before consulting the previous
   result.
8. Implement the final architecture directly. Do not introduce a temporary
   policy that freezes the last subtitle-free metadata, disables adaptive tone
   mapping only during subtitle cues, dims subtitle pixels, raises black-bar
   APL, or conditionally lowers the global peak percentile.
9. Prove the minimal local libplacebo analysis-rectangle fork first. Do not
   submit or push it upstream during local iteration. Any later upstream
   proposal or production decision requires explicit review; retain the public
   shader/detected-metadata design as the no-fork fallback.
10. Keep the behavior renderer-local, configurable for diagnostic comparison,
    and observable without requiring OCR or recognizing subtitle language.

## Acceptance criteria

1. A controlled HDR sample with an unchanged picture and alternating bright
   burned-in bar subtitles produces stable ROI-derived `max_pq_y` and
   `avg_pq_y`; subtitle onset/release does not produce a visible whole-picture
   brightness step beyond an agreed numerical tolerance.
2. Tests prove that subtitle fit and translation still render the complete
   accepted presentation rectangle while analysis uses only the intersection
   of trusted active-picture and visible-source geometry.
3. Tests cover full raster, top/bottom bars, side bars, translated subtitles,
   fitted dense bar content, NLS crop, stale/provisional geometry, and fail-open
   fallback.
4. Scene-cut tests with subtitles already present prove that statistics from
   the preceding scene are not applied to the new scene. Source changes, seeks,
   renderer resets, and generation changes reset analysis state deterministically.
5. Valid Dolby Vision/HDR10+ dynamic metadata bypasses VP-derived analysis and
   retains the existing libplacebo metadata preference contract. Synthetic
   policy tests are mandatory even while the current VP source contract lacks
   end-to-end dynamic-metadata ingress.
6. Native RGB and P010/P210 ingress produce equivalent analysis decisions
   within documented conversion tolerance. SDR input remains unaffected.
7. Diagnostics report presentation crop, trusted picture, resolved analysis
   ROI, metadata source, detected peak/average, fallback reason, and whether an
   analysis result was current or delayed, without per-frame log spam.
8. GPU cost and presentation latency are benchmarked at the supported 4K frame
   rates. The implementation introduces no unbounded CPU readback, renderer
   rebuild, shader compilation, or queue disruption when subtitles toggle.
9. Focused geometry, metadata, scene-boundary, and renderer tests pass, followed
   by the established clean x64 Release build and live A/B validation against
   current full-frame peak detection and peak detection disabled.

## Non-goals

- Do not alter, erase, recolor, dim, OCR, or reconstruct burned-in subtitles.
- Do not make black bars follow picture APL.
- Do not solve arbitrary subtitles embedded inside the trusted active picture;
  that would require a separate reliable pixel mask or semantic detector.
- Do not replace libplacebo tone mapping with MPC Video Renderer's fixed Hable
  curve merely to suppress detector pumping.
- Do not claim that peak-percentile tuning alone satisfies this story.
- Do not ship the spike, expose it as a supported user mode, or deploy it before
  the spike records a GO decision and the production implementation passes all
  acceptance criteria.
- Do not submit, push, open an issue, or create an MR/PR against remote
  libplacebo while the local fork and evidence are still being developed.

## Likely implementation areas

- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.*`
- `src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.*`
- `src/VideoProcessor-Config/ConfigEditorWindow.*` and canonical renderer
  profile schema/defaults for the Zoom / Subtitles toggle
- `src/VideoProcessor-Lib/SceneDetector.*` for an immediate conservative
  hard-cut candidate distinct from later safe-boundary confirmation
- VP Renderer plugin ABI only if new host-visible diagnostics or controls are
  required
- libplacebo RGB hook/detected-HDR-metadata integration or an accepted upstream
  analysis-region API
- local libplacebo fork headers, renderer/shader implementation, tests, build
  provenance, and a uniquely identifiable development runtime
- focused renderer geometry, metadata-precedence, scene-cut, and performance
  tests

## Dependencies and references

- VP-0080 owns fail-safe trusted active-picture crop authority.
- VP-0098 owns active-picture envelopes and final screen fit.
- VP-0110, VP-0122, VP-0129, and VP-0132 own subtitle-aware presentation,
  translation, arbitration, and stability behavior that this story must retain.
- VP-0140 owns non-blocking shader preparation and must not regress.
- libplacebo 7.360.1 public APIs include `pl_shader_detect_peak`,
  `pl_get_detected_hdr_metadata`, and the CIE-Y `max_pq_y`/`avg_pq_y` fields,
  but no independent high-level peak-analysis ROI.
- The current VP `VideoState`/`HDRData` contract carries static mastering,
  MaxCLL, and MaxFALL values but not DV or HDR10+ per-scene structures.
  Production metadata precedence must be future-safe; full end-to-end dynamic
  metadata validation requires that separate ingress capability.
