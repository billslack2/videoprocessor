# VP-0147: Subtitle-safe libplacebo tone-mapping analysis region

## Status

Backlog (2026-08-24). VP Renderer can preserve burned-in subtitles that lie
outside the trusted active picture by expanding or translating its presentation
crop, but libplacebo then uses that same presentation crop for HDR peak and
average-luminance detection. Design and implement a production analysis-region
path that keeps the rendered subtitle intact without allowing bar content to
change the picture tone-mapping curve. Do not ship a cached-metadata or
subtitle-duration freeze as an interim solution.

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

## Required behavior

1. Separate tone-mapping analysis geometry from presentation geometry. The
   full final presentation crop, including accepted burned-in subtitle pixels,
   must still be rendered unchanged.
2. When current trusted active-picture authority exists, derive the analysis
   region from the intersection of the trusted logical active picture and the
   final visible source crop. This excludes revealed black-bar content while
   avoiding influence from picture pixels removed by NLS or presentation crop.
3. Use the same logical analysis region continuously while its geometry remains
   authoritative; do not switch histogram scope merely because subtitle
   detection engages or releases. Full-raster material should naturally retain
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
   a known scene boundary or source generation. Any delayed-analysis design must
   explicitly prevent the prior scene's statistics from governing the first
   frame of a newly detected scene.
8. Implement the final architecture directly. Do not introduce a temporary
   policy that freezes the last subtitle-free metadata, disables adaptive tone
   mapping only during subtitle cues, dims subtitle pixels, raises black-bar
   APL, or conditionally lowers the global peak percentile.
9. Prefer an upstream-supported libplacebo analysis ROI/mask if available. If
   it remains unavailable, use public libplacebo shader/detected-metadata APIs
   in a VP-owned analysis path without an ABI-incompatible private DLL fork.
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
   retains the existing libplacebo metadata preference contract.
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

## Likely implementation areas

- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- `src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.*`
- `src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.*`
- VP Renderer plugin ABI only if new host-visible diagnostics or controls are
  required
- libplacebo RGB hook/detected-HDR-metadata integration or an accepted upstream
  analysis-region API
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

