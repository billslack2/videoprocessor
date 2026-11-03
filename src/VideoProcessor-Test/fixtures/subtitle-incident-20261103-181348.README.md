# Subtitle incident 2026-11-03 18:13:48

The adjacent JSON records telemetry and rendered-output observations from runtime source `43ba61d4c8cfd24b65003d4c887413bd7969d031`. Original HDMI pixels are unavailable. Private recording, screenshots, logs and synthetic diagnostic outputs remain under `artifacts/incident-20261103-181348`; none are embedded in this fixture.

## Missing word: observation and uncertainty

Sources 2037-2040 render the phrase without "be". A rewind-10 player overlay appears during the same incident. The source-2037 upper capture rectangles end at x1854 and restart at x2031, while cleanup covers the intervening region. These facts do not establish whether the incoming word was visible, obscured by the overlay, or rejected by the detector. Do not treat the rendered screenshot as original input evidence.

The isolated synthetic reproducer places bright glyph pixels in that logged gap. Its middle glyph has 4,000 erased pixels and zero captured pixels; adjacent glyphs are captured. Exit code 1 is the expected RED result. This demonstrates an operation-completeness gap, not the original detector's failure.

## Existing contract boundary

The worker already samples the native union of capture and actual erase spans. `NativePixel.line >= 0` identifies capture geometry; `erase` identifies removal; `ink` identifies palette-matching current pixels; `black` identifies neutral near-black. Grouped `ownedInk` proves accepted samples, not absence of overlooked text. Neither backing geometry nor successful residual-bar qualification proves complete glyph capture. Protection counts only when the final shader operation actually passes those current pixels through.

The next repair should enforce one complete-current-operation contract: erased content must be copied current glyphs, independently verified removable backing, or explicitly protected. Unresolved content keeps composition unready and selects current-source fallback. Apply this to first composition as well as reuse; no new per-frame scan or same-baseline exception is implied.

## Smallest next evidence capture

Arm one diagnostic capture. During the existing worker sample loop, latch the first unresolved `erase && line < 0 && ink` witness; this is a diagnostic trigger, not proof that the pixel is text. Retain that exact worker source using the existing bounded frame-packet ownership/writer path. Save one native frame plus identity, format/range, witness coordinate/value, raw/owned bits, capture rectangles, erase/protection masks and backing-proof status. Do not start a later generic recording and assume it contains the offending source. If the bounded writer cannot accept the frame, report a missed capture and keep video advancing.

Use that evidence to reproduce detector classification before changing ownership or grouping. Keep the separate residual-visibility UI fix independently reviewable and tested.
