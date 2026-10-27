# Subtitle background sanity guard

Moved subtitle profiles enable **Guard oversized subtitle backgrounds** by
default. Disable it in Subtitle appearance to retain the original measured
background footprint. Classic and Off do not use this relocation guard.

The guard compares the measured background against the union of every detected
text line. It falls back when the picture-facing margin exceeds the larger of
two line heights or four fallback top margins, when a side exceeds the larger
of two line heights or four fallback side margins, or when left/right margins
differ by more than a line height and the larger exceeds twice the smaller plus
the fallback side margin. These initial thresholds need live scene qualification.
Screen centering is not required. Legitimate off-center and multiline captions
are retained.

Named `[vprenderer.subtitles.NAME]` profiles support:

```ini
background_sanity_guard: true
background_fallback_top_pixels: 12
background_fallback_side_pixels: 20
```

Padding values accept integers 0–200, expressed at 1080 source lines and scaled
to source height (24/40 at 2160 lines). The top margin is mirrored below text
for a top-bar subtitle. The bar-facing edge uses the independently detected
physical bar; only captions intersecting it or within the existing near-bar
distance are eligible. All detected glyphs and sampling fringe are preserved.

The fallback caps existing measured cleanup coverage. It cannot grant new
picture removal or glyph extraction authority. The existing near-bar cleanup
rule closes a measured short gap to the bar. Once triggered, the cap remains
for that cue to avoid oscillation as dark scenery changes. Changes to the guard
policy reset its presentation cache. A diagnostic backing reason of
`background-sanity-padding-fallback` identifies the fallback in existing logs.

This does not change active-picture measurement, subtitle detection, or the
source aspect ratio. Destination styling retains its existing padding policy.
The user's annotated paused scene motivates the guard; screenshots alone are
not a native-frame replay or a live visual acceptance result.
