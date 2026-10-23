# VP-0207: Overlay fit stability and subtitle backing boundaries

## Status
In Progress

Implementation on `codex/subtitle-overlay-stability-20261023`, starting at current beta `dfd34ea6f6365eddb17d986914bb7792836d694b`, in `E:\codex\videoprocessor\subtitle-overlay-stability-20261023`.

## Report
Paused subtitle toggle causes repeated scope/full-raster flashing; playback scrub controls should remain visible in a stable zoomed-out presentation. Subtitle backing lips remain intermittently visible. Screenshot supplied 2026-10-23.

## Evidence and work
Deployed log identifies host 4d1d62d0. Current logs repeatedly reject finite backing with `lip-top-closure-occluded`, `lip-outline-boundary-occluded`, and `lip-black-span-does-not-cover-glyphs`. Shared vertical policy explicitly lets a one-edge translate replace held FIT.

## Acceptance
- Hold a zoomed-out FIT across changing overlay classification for the existing bounded interval; allow immediate outward growth and expire normally.
- Verify finite subtitle backing using glyph-bounded width and a bounded collar above its edge, without extending cleanup into scene shadows.
- Preserve frame/cue ownership, sample budget, native picture transitions and existing cleanup exclusions.
- Pass x64 Release regression tests. Live validation remains required before claiming the reported playback incident resolved.
