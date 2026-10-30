# Aspect-ratio OSD telemetry

The VP Renderer OSD exposes exactly two aspect rows: **AR in use** and **Detected AR**. The HDMI input aspect is not a separate row. Startup uses the renderer's full input-raster geometry until safe picture evidence is available; the input aspect is not hard-coded to 16:9 or derived from the physical screen.

AR in use is the numeric aspect of the final source rectangle after successful render/submission, before downstream NLS/anamorphic display mapping. It follows the selected temporal packet and presentation crop, including subtitle-related geometry. It never substitutes the latest detector candidate or an OSD-only held label for the geometry actually submitted. Existing source-pixel crop retention and safety decisions remain unchanged. The status distinguishes startup fallback, agreement with current detection, unchanged geometry held through different/unavailable evidence, and other presentation adjustments.

Detected AR is the latest evaluated live picture evidence before transition acceptance. Valid provisional bounds are explicitly marked provisional; unavailable measurements stay Unavailable even when the previous crop remains in use. A nonstandard measured ratio is still a valid ratio. Numeric values are displayed to three decimals without snapping to standard ratios; familiar names are annotations only. Both values and source sequences are logged together when they change materially.

No Netflix or 1.66:1 exclusion is introduced. A menu may produce temporary geometry resembling legitimate 1.66:1 content. The two rows distinguish that candidate from an applied change so subsequent captures can guide detector work without hiding actual changes.

Snapshots are published without the render lock on the OSD thread. Source changes and renderer retirement clear incompatible state; output-only cache invalidation does not discard source facts. Shared host/renderer ABI is 34 and requires a matching runtime pair.
