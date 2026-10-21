# PR #181 / #183 integration and regression coverage

PR #183 adds the input-to-output raster readout to the native stats OSD on top
of #181. The review integration starts at beta `71043e19` (including PR #182)
and preserves both original PR histories. The stacks can merge in order:
#181 first, then #183 retargeted to beta.

## Review fixes

- Output raster is a coherent cache snapshot. Verified mode transactions publish
  their read-back; display notifications, mode attempts, failed queries and
  retirement invalidate old data. An epoch rejects a read-back that began before
  a newer display notification. Initialization and deferred display processing
  observe the current Windows source/target mode on the renderer thread, also
  covering Auto/Never and externally initiated changes. UI polling makes no
  additional display-driver calls. Unknown read-back preserves the input-only
  Resolution row.
- Successful ordinary, recreated and temporal presenter geometry updates publish
  the actual drawing size. Failed/replaced swapchains clear old size data.
- Raster tags stay on continuation rows within the existing 540px panel, and the
  height calculation uses the same row builder. The row adds no refresh rate.
  GPU-scaled desktop and differing drawing raster remain distinct diagnostics.
- #181 full-mode routing and two-consecutive-observation verification use shared
  platform-independent production helpers, so tests cover the same decisions
  used by the Windows transaction rather than a test-only copy.

## Added regressions

- Exact tester-profile order (59.94/60, then 29.97 before arbitrary 50/30),
  multiples/divisors with stable raster ties, unavailable modes, interlaced field
  cadence and fixed-output fallback.
- GDI raster-change versus CCD rate-only/scaled-desktop restoration routing;
  both desktop and signal raster; missing target mode; refresh/scan mismatch;
  tight driver rounding; consecutive observations with query/mismatch resets.
- Disabled/nested DPI scopes restore the caller context; a hidden physical popup
  produces physical client dimensions without leaking thread DPI context.
- Native, GPU-scaled, windowed/drawn, interlaced and unknown OSD output; malformed
  payloads; restoration/recreation; invalidation and stale observations; coherent
  concurrent geometry pairs; no rate on the row; actual 20px Consolas width and
  matching continuation-row counts.

The earlier tester run qualified Windows mode read-back and geometry on a
combined build with the projector off. Automated tests do not replace projected
picture, independent receiver timing or visually checked 1:1/native OSD sizing.
No display-mode changes are made by the new tests.
