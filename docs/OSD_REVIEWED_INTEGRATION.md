# Reviewed OSD integration

This change integrates the original work from PRs #173, #174 and #178 onto
`v1.4.02-beta` after the reviewed #183/#184 raster stack. The original author
commits remain in the integration history.

## Telemetry contracts

- EOTF and input primaries describe state accepted by the current renderer.
  A rejected candidate or state retained for a replacement renderer is not
  presented as active input. A differing capture observation is identified as
  the wire value; unavailable effective state remains explicit.
- HDR source metadata and detected light belong to the same processed source
  frame. Temporal output reuses the measurements captured with its selected
  source texture, rather than querying whichever neighbour rendered last.
  Measurements describe that selected source, not the luminance of a final
  blended image. Stable libplacebo may not supply a linear maxRGB average.
- Source, processing and output lifecycle changes withdraw obsolete HDR
  snapshots and reject publications that started in an older context. An
  unchanged paused frame has no age-based expiry.
- Unknown static CLL/FALL are displayed as unknown; a valid measured black
  level remains zero. Nonfinite or negative values are not printed as light.
- Output contract strings are cached independently of WIRE/DESKTOP/DRAWN
  annotations. Each read assembles raster annotations from the current
  coherent raster snapshot. Mode changes, pending resize and retirement
  cannot restore an older raster through a formatted-string cache.
- A supported getter returning an empty value is authoritative invalidation.
  Host fallback is reserved for unsupported getters in the same telemetry
  generation. LUT load, rejection and disable transitions publish directly.

## Native layout

One production row pipeline controls label/value alignment, measured overflow,
section spacing, bitmap height and drawing. Values and continuation rows share
one column. Operator-controlled names use measured ellipsis; long numeric
HDR diagnostics preserve their values through aligned continuation rows.
Units are stated in the HDR row labels. Optional detail markers with no value
allocate no row. The native panel retains equal outer padding.

## Regression coverage

The native test project registers these suites alongside the existing raster,
placement, motion, configuration and plugin-lifecycle coverage:

- `OutputOsdTelemetryCacheTests`: actual getter assembly under forced render
  lock contention, invalidation races, live raster replacement, paused reads,
  LUT transitions and retirement.
- `HdrStatusTests`: source/detector coherence, actual D3D11 temporal cue reuse,
  processing epochs, snapshot lifecycle, HDR/SDR transitions, numeric validity
  and optional status layout/version rejection.
- `OsdInputColorInfoTests`: Dolby Vision tunnel, LLDV, primaries overrides,
  accepted/rejected/stopping state and wire suffix semantics.
- `StatsOverlayLayoutTests`: real native bitmap rows, font extents, shared
  dimensions, optional sections, continuation alignment and name overflow.

Test counts and exact clean source pins are recorded in the integration PR.
Native raster tests do not establish projected appearance or physical sizing.
