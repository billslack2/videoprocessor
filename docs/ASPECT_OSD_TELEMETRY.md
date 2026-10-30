# Aspect-ratio OSD telemetry

The VP Renderer OSD exposes exactly two aspect rows: **AR in use** and **Detected AR**. The HDMI input aspect is not a separate row. Startup uses the renderer's full input-raster geometry until safe picture evidence is available; the input aspect is not hard-coded to 16:9 or derived from the physical screen.

AR in use is the numeric aspect of the final source rectangle after successful render/submission, before downstream NLS/anamorphic display mapping. It follows the selected temporal packet and presentation crop, including subtitle-related geometry. It never substitutes the latest detector candidate or an OSD-only held label for the geometry actually submitted. Existing source-pixel crop retention and safety decisions remain unchanged. The status distinguishes startup fallback, agreement with current detection, unchanged geometry held through different/unavailable evidence, and other presentation adjustments.

Detected AR is the latest evaluated live picture evidence before transition acceptance. Valid provisional bounds are explicitly marked provisional; unavailable measurements stay Unavailable even when the previous crop remains in use. A nonstandard measured ratio is still a valid ratio. Numeric values are displayed to three decimals without snapping to standard ratios; familiar names are annotations only. Both values and source sequences are logged together when they change materially.

No Netflix or 1.66:1 exclusion is introduced. VP-0222 rejects incomplete-axis pillarbox acquisition and withdraws an established side crop after three adjacent native observations show broad picture in both old side bars. Genuine 1.66:1 remains supported. The two rows distinguish live candidates from applied geometry; unavailable analysis views explicitly clear live detection. See VP-0222_PILLARBOX_AUTHORITY.md for the evidence and regression contract.

Snapshots are published without the render lock on the OSD thread. Source changes and renderer retirement clear incompatible state; output-only cache invalidation does not discard source facts. Shared host/renderer ABI is 34 and requires a matching runtime pair.
